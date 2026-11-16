/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * An RGB panel on the LCD side of LCD-CAM. A loop of DMA descriptors covers each framebuffer in
 * PSRAM and the controller asks for the next frame by itself: once started, the panel gets its
 * frames without the CPU, which only relinks the loops for a swap. With one framebuffer writes
 * land in the one the panel shows, so a frame can show half of an update. With two
 * (CONFIG_DISPLAY_ESP32_LCD_RGB_DOUBLE_BUFFER) they land in the other, and the write that ends a
 * frame hands it over: the shown loop's last descriptor is pointed at the other loop while the DMA
 * is still far from it, and the DMA goes over as that frame ends. Where the DMA is decides every
 * step. DISPLAY_EVENT_VSYNC comes as the VSYNC pulse starts, its width and the back porch before
 * the first line.
 *
 * The sequence follows ESP-IDF's esp_lcd_panel_rgb.c, but for one thing: the controller starts
 * before the DMA, with the whole of the first blanking for the FIFO to fill. Started the other way
 * round, the first pixel comes out one too late if the DMA's first read is slow, with PSRAM at
 * 40 MHz or with the CPU using it, and the whole picture with it.
 */

#define DT_DRV_COMPAT espressif_esp32_lcd_cam_rgb

#include <string.h>

#include <zephyr/cache.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/display/esp32_lcd_rgb.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/interrupt_controller/intc_esp32.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/dt-bindings/display/panel.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/multi_heap/shared_multi_heap.h>
#include <zephyr/sys/barrier.h>
#include <zephyr/sys/util.h>

#include <esp_attr.h>
#include <esp_clk_tree.h>
#include <esp_intr_alloc.h>
#include <esp_private/esp_clk_tree_common.h>
#include <esp_private/periph_ctrl.h>
#include <esp_system.h>
#include <hal/axi_dma_ll.h>
#include <hal/clk_tree_ll.h>
#include <hal/dma_types.h>
#include <hal/gdma_channel.h>
#include <hal/gdma_ll.h>
#include <hal/lcd_ll.h>
#include <hal/misc.h>
#include <soc/hp_sys_clkrst_struct.h>

LOG_MODULE_REGISTER(display_esp32_lcd_rgb, CONFIG_DISPLAY_LOG_LEVEL);

#define LCD_RGB_DMA_GROUP  1
/* The most this DMA takes at a time from PSRAM. */
#define LCD_RGB_DMA_BURST  128U
#define LCD_RGB_DMA_PRIO   GDMA_LL_CHANNEL_MAX_PRIORITY
/* A descriptor holds 4095 bytes; a multiple of the burst keeps every read a whole burst. */
#define LCD_RGB_DESC_BYTES ROUND_DOWN(DMA_DESCRIPTOR_BUFFER_MAX_SIZE, LCD_RGB_DMA_BURST)
#define LCD_RGB_FB_ALIGN   LCD_RGB_DMA_BURST

#define LCD_RGB_SRC_HZ     160000000U
/* From the LCD clock to the pixel clock, as IDF: "due to some unstable hardware issue". */
#define LCD_RGB_PCLK_DIV   2U
/* The smallest divider to the LCD clock: IDF's min_integ, for which it gives no reason. */
#define LCD_RGB_LCD_DIV_MIN 2U
/* Far longer than one burst takes from PSRAM. */
#define LCD_RGB_ABORT_SPINS 1000U
/* The core clock LCD and camera share: PLL_F120M / 2, as IDF sets it. */
#define LCD_RGB_CORE_SRC_PLL120M 1U
#define LCD_RGB_CORE_DIV         2U

/* The write-back masks interrupts for the range it gets: in pieces, as the DSI driver does. */
#define LCD_RGB_FLUSH_CHUNK 4096U

#ifdef CONFIG_DISPLAY_ESP32_LCD_RGB_DOUBLE_BUFFER
#define LCD_RGB_FB_NUM 2U
#else
#define LCD_RGB_FB_NUM 1U
#endif
/* The areas a frame writes, kept apart up to this many: what to copy to the other buffer. */
#define LCD_RGB_AREAS     8U
/* A swap takes two frame starts at most: 34 ms at 58 Hz. */
#define LCD_RGB_SWAP_WAIT  K_MSEC(100)
/* Pointed elsewhere only this far ahead of the DMA: it reads a descriptor's link as it loads it. */
#define LCD_RGB_ARM_AHEAD  2U
/* At a frame's start the DMA has filled the FIFO from the first descriptors of a loop. */
#define LCD_RGB_START_DESC 2U

struct lcd_rgb_area {
	uint16_t x;
	uint16_t y;
	uint16_t w;
	uint16_t h;
};

enum lcd_rgb_swap {
	LCD_RGB_SWAP_NONE,
	/* A frame is written: at the next frame's start the shown ring is sent on to it. */
	LCD_RGB_SWAP_ASKED,
	/* Sent on: at the next frame's start the panel shows it. */
	LCD_RGB_SWAP_ARMED,
};

struct lcd_rgb_config {
	const struct pinctrl_dev_config *pcfg;
	struct gpio_dt_spec backlight;
	/* A ring of desc_count for each framebuffer. */
	dma_descriptor_align8_t *desc;
	size_t desc_count;
	uint32_t pclk_hz;
	bool apll;
	uint16_t width;
	uint16_t height;
	uint16_t hsync_len;
	uint16_t hback_porch;
	uint16_t hfront_porch;
	uint16_t vsync_len;
	uint16_t vback_porch;
	uint16_t vfront_porch;
	uint8_t hsync_active;
	uint8_t vsync_active;
	uint8_t de_active;
	uint8_t pixelclk_active;
	uint8_t dma_channel;
	int irq_source;
	int irq_priority;
	int irq_flags;
};

struct lcd_rgb_data {
	uint8_t *fb[LCD_RGB_FB_NUM];
	size_t fb_size;
	/* The framebuffer the panel shows. */
	uint8_t front;
	/* With two: the swap (under lock), its end, and the writers' order. */
	enum lcd_rgb_swap swap;
	struct k_sem swapped;
	struct k_mutex fb_lock;
	/* Written into the other buffer since the last frame's end; written in the frame shown. */
	struct lcd_rgb_area drawn[LCD_RGB_AREAS];
	uint8_t drawn_n;
	struct lcd_rgb_area shown[LCD_RGB_AREAS];
	uint8_t shown_n;
	struct k_spinlock lock;
	display_event_cb_t event_cb;
	void *event_user_data;
	uint32_t event_mask;
	bool underrun;
	/* A wait for a frame's start (under lock), its semaphore, the blanking from there. */
	bool blank_asked;
	struct k_sem blank_begun;
	uint32_t blank_us;
};

static inline lcd_cam_dev_t *lcd_rgb_hw(void)
{
	return LCD_LL_GET_HW(0);
}

static inline dma_descriptor_align8_t *lcd_rgb_ring(const struct lcd_rgb_config *config,
						     uint32_t fb)
{
	return &config->desc[fb * config->desc_count];
}

/* The descriptor the DMA is at: its index in its loop, and the loop's framebuffer in @p fb. */
static IRAM_ATTR uint32_t lcd_rgb_dma_at(const struct lcd_rgb_config *config, uint32_t *fb)
{
	uintptr_t at = axi_dma_ll_tx_get_prefetched_desc_addr(AXI_DMA_LL_GET_HW(0),
							      config->dma_channel);
	uintptr_t i = (at - (uintptr_t)config->desc) / sizeof(config->desc[0]);

	if (at < (uintptr_t)config->desc || i >= LCD_RGB_FB_NUM * config->desc_count) {
		*fb = LCD_RGB_FB_NUM;
		return 0U;
	}
	*fb = i / config->desc_count;
	return i % config->desc_count;
}

/* Under lock: send the shown loop on to the other one if the DMA is far enough from its end. */
static IRAM_ATTR bool lcd_rgb_arm(const struct lcd_rgb_config *config, struct lcd_rgb_data *data)
{
	dma_descriptor_align8_t *shown = lcd_rgb_ring(config, data->front);
	uint32_t fb, i = lcd_rgb_dma_at(config, &fb);

	if (fb != data->front || i + LCD_RGB_ARM_AHEAD >= config->desc_count) {
		return false;
	}
	shown[config->desc_count - 1U].next = lcd_rgb_ring(config, data->front ^ 1U);
	barrier_dmem_fence_full();
	data->swap = LCD_RGB_SWAP_ARMED;
	return true;
}

static void lcd_rgb_flush(void *addr, size_t size)
{
	uint8_t *p = addr;

	while (size > 0U) {
		size_t n = MIN(size, (size_t)LCD_RGB_FLUSH_CHUNK);

		sys_cache_data_flush_range(p, n);
		p += n;
		size -= n;
	}
}

static IRAM_ATTR void lcd_rgb_event(const struct device *dev, uint32_t evt)
{
	struct lcd_rgb_data *data = dev->data;
	k_spinlock_key_t key = k_spin_lock(&data->lock);
	display_event_cb_t cb = data->event_cb;
	uint32_t mask = data->event_mask;
	void *user_data = data->event_user_data;

	k_spin_unlock(&data->lock, key);

	if (cb != NULL && (mask & evt) != 0U) {
		struct display_event_data event = {0};

		(void)cb(dev, evt, &event, user_data);
	}
}

/* An AXI transfer cannot be reset while it is still in flight. */
static IRAM_ATTR void lcd_rgb_dma_reset(axi_dma_dev_t *dma, uint32_t ch)
{
	axi_dma_ll_tx_abort(dma, ch, true);
	for (uint32_t i = 0U; i < LCD_RGB_ABORT_SPINS; i++) {
		if (axi_dma_ll_tx_is_reset_avail(dma, ch)) {
			break;
		}
	}
	axi_dma_ll_tx_reset_channel(dma, ch);
	axi_dma_ll_tx_abort(dma, ch, false);
}

/*
 * What the controller asks for and what the DMA delivers are only tied together by their start: an
 * underrun loses pixels from the panel's frame, and every later frame shows shifted by as many. So
 * the frame after one starts both over, the way the first start does. Less than that does not do:
 * without its reset the controller keeps pixels from before, up to 1328 of them.
 */
static IRAM_ATTR void lcd_rgb_restart(const struct lcd_rgb_config *config, uint32_t front)
{
	axi_dma_dev_t *dma = AXI_DMA_LL_GET_HW(0);
	lcd_cam_dev_t *hw = lcd_rgb_hw();

	lcd_rgb_dma_reset(dma, config->dma_channel);
	lcd_ll_stop(hw);
	lcd_ll_reset(hw);
	lcd_ll_fifo_reset(hw);
	lcd_ll_start(hw);
	axi_dma_ll_tx_set_desc_addr(dma, config->dma_channel,
				    (uint32_t)lcd_rgb_ring(config, front));
	axi_dma_ll_tx_start(dma, config->dma_channel);
}

/*
 * At a frame's start, under lock, with the DMA in loop @p fb. Once it has gone over to the other
 * loop the panel shows the other buffer, and the first loop closes again. A swap asked for and not
 * yet linked is linked now, the DMA being at the start of the shown loop.
 */
static IRAM_ATTR void lcd_rgb_swap_step(const struct lcd_rgb_config *config,
					struct lcd_rgb_data *data, uint32_t fb)
{
	dma_descriptor_align8_t *shown = lcd_rgb_ring(config, data->front);

	if (data->swap == LCD_RGB_SWAP_ARMED && fb == (data->front ^ 1U)) {
		shown[config->desc_count - 1U].next = &shown[0];
		barrier_dmem_fence_full();
		data->front ^= 1U;
		data->swap = LCD_RGB_SWAP_NONE;
		k_sem_give(&data->swapped);
	} else if (data->swap == LCD_RGB_SWAP_ASKED) {
		(void)lcd_rgb_arm(config, data);
	}
}

static IRAM_ATTR void lcd_rgb_isr(void *arg)
{
	const struct device *dev = arg;
	const struct lcd_rgb_config *config = dev->config;
	struct lcd_rgb_data *data = dev->data;
	lcd_cam_dev_t *hw = lcd_rgb_hw();
	uint32_t status = lcd_ll_get_interrupt_status(hw);

	lcd_ll_clear_interrupt_status(hw, status);
	if ((status & LCD_LL_EVENT_UNDERRUN) != 0U) {
		/*
		 * It fires for every pixel missed, which would take the CPU the DMA is short of:
		 * off until the restart. Nothing else writes this register once running.
		 */
		(lcd_ll_enable_interrupt)(hw, LCD_LL_EVENT_UNDERRUN, false);
		data->underrun = true;
		lcd_rgb_event(dev, DISPLAY_EVENT_FIFO_UNDERFLOW);
	}
	if ((status & LCD_LL_EVENT_VSYNC_END) != 0U) {
		k_spinlock_key_t key = k_spin_lock(&data->lock);
		uint32_t fb, at = lcd_rgb_dma_at(config, &fb);

		/* An event served late, the frame under way, is no blanking. */
		if (data->blank_asked && at <= LCD_RGB_START_DESC) {
			data->blank_asked = false;
			k_sem_give(&data->blank_begun);
		}
		if (LCD_RGB_FB_NUM > 1U) {
			lcd_rgb_swap_step(config, data, fb);
		}
		k_spin_unlock(&data->lock, key);
		if (data->underrun) {
			data->underrun = false;
			lcd_rgb_restart(config, data->front);
			lcd_ll_clear_interrupt_status(hw, LCD_LL_EVENT_UNDERRUN);
			(lcd_ll_enable_interrupt)(hw, LCD_LL_EVENT_UNDERRUN, true);
		}
		lcd_rgb_event(dev, DISPLAY_EVENT_VSYNC);
	}
}

int display_esp32_lcd_rgb_wait_blanking(const struct device *dev, k_timeout_t timeout,
					 uint32_t *blank_us)
{
	struct lcd_rgb_data *data = dev->data;
	k_spinlock_key_t key = k_spin_lock(&data->lock);
	int ret;

	k_sem_reset(&data->blank_begun);
	data->blank_asked = true;
	k_spin_unlock(&data->lock, key);

	ret = k_sem_take(&data->blank_begun, timeout);
	if (ret != 0) {
		key = k_spin_lock(&data->lock);
		data->blank_asked = false;
		k_spin_unlock(&data->lock, key);
		return -EAGAIN;
	}
	if (blank_us != NULL) {
		*blank_us = data->blank_us;
	}
	return 0;
}

static void lcd_rgb_dma_start(const struct lcd_rgb_config *config)
{
	axi_dma_dev_t *dma = AXI_DMA_LL_GET_HW(0);
	uint32_t ch = config->dma_channel;

	PERIPH_RCC_ATOMIC() {
		gdma_ll_enable_bus_clock(LCD_RGB_DMA_GROUP, true);
		gdma_ll_reset_register(LCD_RGB_DMA_GROUP);
	}
	axi_dma_ll_force_enable_reg_clock(dma, true);
	axi_dma_ll_set_default_memory_range(dma);
	axi_dma_ll_tx_reset_channel(dma, ch);
	axi_dma_ll_tx_enable_owner_check(dma, ch, false);
	axi_dma_ll_tx_enable_auto_write_back(dma, ch, false);
	axi_dma_ll_tx_set_eof_mode(dma, ch, 1);
	axi_dma_ll_tx_enable_data_burst(dma, ch, true);
	axi_dma_ll_tx_enable_descriptor_burst(dma, ch, true);
	axi_dma_ll_tx_set_burst_size(dma, ch, LCD_RGB_DMA_BURST);
	axi_dma_ll_tx_set_priority(dma, ch, LCD_RGB_DMA_PRIO);
	axi_dma_ll_tx_disconnect_all(dma, ch);
	axi_dma_ll_tx_connect_to_periph(dma, ch, SOC_GDMA_TRIG_PERIPH_LCD0);
	axi_dma_ll_tx_clear_interrupt_status(dma, ch, AXI_DMA_LL_TX_EVENT_MASK);
	axi_dma_ll_tx_set_desc_addr(dma, ch, (uint32_t)lcd_rgb_ring(config, 0U));
	axi_dma_ll_tx_start(dma, ch);
}

/* lcd_clk = 160 MHz / (n + b / a), pixel clock = lcd_clk / 2. */
static uint32_t lcd_rgb_clock_calc(uint32_t pclk_hz, uint32_t *n, uint32_t *a, uint32_t *b)
{
	uint32_t lcd_hz = pclk_hz * LCD_RGB_PCLK_DIV;
	uint32_t rem;

	*n = LCD_RGB_SRC_HZ / lcd_hz;
	rem = LCD_RGB_SRC_HZ - *n * lcd_hz;
	*a = 0U;
	*b = 0U;
	if (rem != 0U) {
		uint32_t x = lcd_hz, y = rem;

		while (y != 0U) {
			uint32_t t = x % y;

			x = y;
			y = t;
		}
		*a = lcd_hz / x;
		*b = rem / x;
		if (*a >= LCD_LL_CLK_FRAC_DIV_AB_MAX) {
			*b = (uint32_t)(((uint64_t)*b * (LCD_LL_CLK_FRAC_DIV_AB_MAX - 1U) +
					 *a / 2U) / *a);
			*a = LCD_LL_CLK_FRAC_DIV_AB_MAX - 1U;
			if (*b == *a) {
				/* Rounded up to a whole one. */
				*n += 1U;
				*a = 0U;
				*b = 0U;
			}
		}
	}
	if (*a != 0U) {
		return (uint32_t)((uint64_t)LCD_RGB_SRC_HZ * *a / (*n * *a + *b)) /
		       LCD_RGB_PCLK_DIV;
	}
	return LCD_RGB_SRC_HZ / *n / LCD_RGB_PCLK_DIV;
}

/*
 * The VCO rtc_clk_apll_coeff_calc() would pick for an APLL frequency: the first output divider that
 * puts it at 350 MHz or more. It does not check the top, 500 MHz.
 */
static bool lcd_rgb_apll_fits(uint32_t apll_hz)
{
	int32_t o_div = (int32_t)(CLK_LL_APLL_MULTIPLIER_MIN_HZ / (2U * apll_hz)) - 1;

	if (apll_hz < CLK_LL_APLL_MIN_HZ || apll_hz > CLK_LL_APLL_MAX_HZ) {
		return false;
	}
	return o_div < 0 ||
	       (uint64_t)apll_hz * 2U * (uint32_t)(o_div + 2) <= CLK_LL_APLL_MULTIPLIER_MAX_HZ;
}

/*
 * The APLL at the highest whole multiple of the LCD clock it can take, so that the LCD clock's
 * divider has no fraction and adds no pattern.
 */
static int lcd_rgb_apll_start(const struct lcd_rgb_config *config, uint32_t *n, uint32_t *real_hz)
{
	uint32_t lcd_hz = config->pclk_hz * LCD_RGB_PCLK_DIV;
	uint32_t apll_hz;
	esp_err_t err;

	*n = MIN(CLK_LL_APLL_MAX_HZ / lcd_hz, LCD_LL_CLK_FRAC_DIV_N_MAX);
	while (*n >= LCD_RGB_LCD_DIV_MIN && !lcd_rgb_apll_fits(*n * lcd_hz)) {
		*n -= 1U;
	}
	if (*n < LCD_RGB_LCD_DIV_MIN) {
		LOG_ERR("no pixel clock of %u Hz from the APLL", config->pclk_hz);
		return -EINVAL;
	}
	(void)esp_clk_tree_enable_src(SOC_MOD_CLK_APLL, true);
	err = esp_clk_tree_src_set_freq_hz(SOC_MOD_CLK_APLL, *n * lcd_hz, &apll_hz);
	if (err != ESP_OK) {
		(void)esp_clk_tree_enable_src(SOC_MOD_CLK_APLL, false);
		LOG_ERR("APLL at %u Hz: %d", *n * lcd_hz, err);
		return err == ESP_ERR_INVALID_STATE ? -EBUSY : -EINVAL;
	}
	*real_hz = apll_hz / (*n * LCD_RGB_PCLK_DIV);
	return 0;
}

static int lcd_rgb_start(const struct device *dev)
{
	const struct lcd_rgb_config *config = dev->config;
	struct lcd_rgb_data *data = dev->data;
	lcd_cam_dev_t *hw = lcd_rgb_hw();
	uint32_t n, a, b, real_hz, line;
	int ret;

	if (config->apll) {
		ret = lcd_rgb_apll_start(config, &n, &real_hz);
		if (ret != 0) {
			return ret;
		}
		a = 0U;
		b = 0U;
	} else {
		real_hz = lcd_rgb_clock_calc(config->pclk_hz, &n, &a, &b);
		if (n < LCD_RGB_LCD_DIV_MIN || n > LCD_LL_CLK_FRAC_DIV_N_MAX) {
			LOG_ERR("no divider for a pixel clock of %u Hz", config->pclk_hz);
			return -EINVAL;
		}
	}

	PERIPH_RCC_ATOMIC() {
		/* The macro of this name wants another critical section than this one. */
		(lcd_ll_enable_bus_clock)(0, true);
		_lcd_ll_reset_register(0);
		HP_SYS_CLKRST.lcdcam_lcdcam_ctrl0.reg_lcdcam_clk_src_sel =
			LCD_RGB_CORE_SRC_PLL120M;
		HAL_FORCE_MODIFY_U32_REG_FIELD(HP_SYS_CLKRST.lcdcam_lcdcam_ctrl0,
					       reg_lcdcam_clk_div_num, LCD_RGB_CORE_DIV - 1U);
		HAL_FORCE_MODIFY_U32_REG_FIELD(HP_SYS_CLKRST.lcdcam_lcdcam_ctrl0,
					       reg_lcdcam_clk_div_denominator, 0U);
		HAL_FORCE_MODIFY_U32_REG_FIELD(HP_SYS_CLKRST.lcdcam_lcdcam_ctrl0,
					       reg_lcdcam_clk_div_numerator, 0U);
		lcd_ll_enable_clock(hw, true);
		lcd_ll_select_clk_src(hw, config->apll ? LCD_CLK_SRC_APLL : LCD_CLK_SRC_PLL160M);
		lcd_ll_set_group_clock_coeff(hw, (int)n, (int)a, (int)b);
	}
	lcd_ll_mem_set_low_power_mode(hw, LCD_LL_MEM_LP_MODE_SHUT_DOWN);
	lcd_ll_mem_power_by_pmu(hw);
	/* It improves the performance and prevents underruns, IDF says. */
	lcd_ll_enable_trans_buffer(hw, true);
	lcd_ll_reset(hw);
	lcd_ll_fifo_reset(hw);

	PERIPH_RCC_ATOMIC() {
		lcd_ll_enable_interrupt(hw, LCD_LL_EVENT_RGB, false);
	}
	lcd_ll_clear_interrupt_status(hw, UINT32_MAX);
	ret = esp_intr_alloc(config->irq_source,
			     ESP_PRIO_TO_FLAGS(config->irq_priority) |
				     ESP_INT_FLAGS_CHECK(config->irq_flags),
			     lcd_rgb_isr, (void *)dev, NULL);
	if (ret != 0) {
		LOG_ERR("interrupt: %d", ret);
		if (config->apll) {
			(void)esp_clk_tree_enable_src(SOC_MOD_CLK_APLL, false);
		}
		return ret;
	}

	lcd_ll_set_data_wire_width(hw, 16);
	lcd_ll_set_pixel_clock_prescale(hw, LCD_RGB_PCLK_DIV);
	lcd_ll_set_clock_idle_level(hw, false);
	/* Set, PCLK is high in a data cycle's first half: data changes on its rising edge. */
	lcd_ll_set_pixel_clock_edge(hw, config->pixelclk_active != 0U);
	lcd_ll_enable_rgb_mode(hw, true);
	lcd_ll_set_dma_read_stride(hw, 16);
	lcd_ll_enable_color_convert(hw, false);
	lcd_ll_set_phase_cycles(hw, 0, 0, 1);
	/* The DMA buffer's size decides how many data cycles there are. */
	lcd_ll_enable_output_always_on(hw, true);
	lcd_ll_set_idle_level(hw, config->hsync_active == 0U, config->vsync_active == 0U,
			      config->de_active == 0U);
	lcd_ll_set_blank_cycles(hw, 1, 1);
	lcd_ll_set_horizontal_timing(hw, config->hsync_len, config->hback_porch, config->width,
				     config->hfront_porch);
	lcd_ll_set_vertical_timing(hw, config->vsync_len, config->vback_porch, config->height,
				   config->vfront_porch);
	lcd_ll_enable_output_hsync_in_porch_region(hw, true);
	lcd_ll_set_hsync_position(hw, 0);
	/* The stream: after a frame the controller asks the DMA for the next by itself. */
	lcd_ll_enable_auto_next_frame(hw, true);

	PERIPH_RCC_ATOMIC() {
		lcd_ll_enable_interrupt(hw, LCD_LL_EVENT_RGB, true);
	}

	lcd_ll_stop(hw);
	lcd_ll_reset(hw);
	lcd_ll_fifo_reset(hw);
	lcd_ll_start(hw);
	lcd_rgb_dma_start(config);

	line = config->width + config->hsync_len + config->hback_porch + config->hfront_porch;
	/* From the event, as VSYNC starts, to the first line. */
	data->blank_us = (uint32_t)((uint64_t)(config->vsync_len + config->vback_porch) * line *
				    USEC_PER_SEC / real_hz);
	LOG_INF("%ux%u, pixel clock %u Hz (asked %u), %u Hz a frame", config->width, config->height,
		real_hz, config->pclk_hz,
		real_hz / (line * (config->height + config->vsync_len + config->vback_porch +
				   config->vfront_porch)));
	return 0;
}

static int lcd_rgb_blanking_on(const struct device *dev)
{
	const struct lcd_rgb_config *config = dev->config;

	return config->backlight.port != NULL ? gpio_pin_set_dt(&config->backlight, 0) : -ENOSYS;
}

static int lcd_rgb_blanking_off(const struct device *dev)
{
	const struct lcd_rgb_config *config = dev->config;

	return config->backlight.port != NULL ? gpio_pin_set_dt(&config->backlight, 1) : -ENOSYS;
}

static bool lcd_rgb_area_valid(const struct lcd_rgb_config *config, uint16_t x, uint16_t y,
			       const struct display_buffer_descriptor *desc, const void *buf)
{
	if (x + desc->width > config->width || y + desc->height > config->height ||
	    desc->pitch < desc->width) {
		return false;
	}
	if (desc->width == 0U || desc->height == 0U) {
		return true;
	}
	/* LVGL's buf_size leaves out the pitch's padding: only the pixels can be checked. */
	return buf != NULL &&
	       (size_t)desc->width * desc->height * sizeof(uint16_t) <= desc->buf_size;
}

static void lcd_rgb_copy_rows(const struct lcd_rgb_config *config, uint8_t *dst_fb,
			      const uint8_t *src, size_t src_stride, const struct lcd_rgb_area *a)
{
	const size_t bpp = sizeof(uint16_t);
	const size_t stride = config->width * bpp;
	uint8_t *first = dst_fb + a->y * stride + a->x * bpp;
	uint8_t *dst = first;

	for (uint16_t row = 0; row < a->h; row++) {
		memcpy(dst, src, a->w * bpp);
		dst += stride;
		src += src_stride;
	}
	lcd_rgb_flush(first, (size_t)(a->h - 1U) * stride + a->w * bpp);
}

/* LVGL sends an area in bands of rows: one touching another as wide makes that one taller. */
static int lcd_rgb_area_band(const struct lcd_rgb_area *list, uint8_t n,
			     const struct lcd_rgb_area *a)
{
	for (uint8_t i = 0; i < n; i++) {
		const struct lcd_rgb_area *b = &list[i];

		if (b->x == a->x && b->w == a->w && a->y <= b->y + b->h && b->y <= a->y + a->h) {
			return i;
		}
	}
	return -1;
}

/* Into the list: exactly while there is room, then the last one grows around it. */
static void lcd_rgb_area_add(struct lcd_rgb_area *list, uint8_t *n, const struct lcd_rgb_area *a)
{
	int i = lcd_rgb_area_band(list, *n, a);
	struct lcd_rgb_area *b;
	uint16_t x0, y0;

	if (i < 0 && *n < LCD_RGB_AREAS) {
		list[(*n)++] = *a;
		return;
	}
	b = &list[i >= 0 ? i : *n - 1];
	x0 = MIN(a->x, b->x);
	y0 = MIN(a->y, b->y);
	b->w = MAX(a->x + a->w, b->x + b->w) - x0;
	b->h = MAX(a->y + a->h, b->y + b->h) - y0;
	b->x = x0;
	b->y = y0;
}

/* Of @p a, what no area of @p drawn covers: from @p src to @p dst, row by row. */
static void lcd_rgb_copy_undrawn(const struct lcd_rgb_config *config, uint8_t *dst,
				 const uint8_t *src, const struct lcd_rgb_area *a,
				 const struct lcd_rgb_area *drawn, uint8_t n)
{
	const size_t bpp = sizeof(uint16_t);
	const size_t stride = config->width * bpp;

	for (uint16_t y = a->y; y < a->y + a->h; y++) {
		uint16_t x = a->x;
		const uint16_t end = a->x + a->w;

		while (x < end) {
			uint16_t next = end;
			bool covered = false;

			for (uint8_t i = 0; i < n && !covered; i++) {
				const struct lcd_rgb_area *d = &drawn[i];

				if (y < d->y || y >= d->y + d->h || d->x + d->w <= x) {
					continue;
				}
				if (d->x <= x) {
					x = d->x + d->w;
					covered = true;
				} else {
					next = MIN(next, d->x);
				}
			}
			if (!covered) {
				memcpy(dst + y * stride + x * bpp, src + y * stride + x * bpp,
				       (size_t)(next - x) * bpp);
				x = next;
			}
		}
	}
	lcd_rgb_flush(dst + a->y * stride + a->x * bpp,
		      (size_t)(a->h - 1U) * stride + a->w * bpp);
}

/*
 * The other buffer misses what the shown frame drew: copied from the shown buffer at the end of
 * the frame written now, but for what this frame drew itself, newer.
 */
static void lcd_rgb_catch_up(const struct device *dev)
{
	const struct lcd_rgb_config *config = dev->config;
	struct lcd_rgb_data *data = dev->data;

	for (uint8_t i = 0; i < data->shown_n; i++) {
		lcd_rgb_copy_undrawn(config, data->fb[data->front ^ 1U], data->fb[data->front],
				     &data->shown[i], data->drawn, data->drawn_n);
	}
	data->shown_n = 0U;
}

/*
 * Once a swap asked for is done: the panel has let go of the other buffer. Up to two frame starts.
 * Without them (interrupts held off that long) the DMA decides: gone over, the swap is done;
 * otherwise it is undone, and the frame stays in the other buffer, drawn, for the next one.
 */
static void lcd_rgb_swap_wait(const struct device *dev)
{
	const struct lcd_rgb_config *config = dev->config;
	struct lcd_rgb_data *data = dev->data;
	dma_descriptor_align8_t *shown;
	k_spinlock_key_t key;
	bool undone = false;
	uint32_t fb;

	if (data->swap == LCD_RGB_SWAP_NONE ||
	    k_sem_take(&data->swapped, LCD_RGB_SWAP_WAIT) == 0) {
		return;
	}
	key = k_spin_lock(&data->lock);
	shown = lcd_rgb_ring(config, data->front);
	(void)lcd_rgb_dma_at(config, &fb);
	if (data->swap == LCD_RGB_SWAP_ARMED && fb == (data->front ^ 1U)) {
		shown[config->desc_count - 1U].next = &shown[0];
		barrier_dmem_fence_full();
		data->front ^= 1U;
	} else if (data->swap != LCD_RGB_SWAP_NONE) {
		shown[config->desc_count - 1U].next = &shown[0];
		barrier_dmem_fence_full();
		undone = true;
	}
	data->swap = LCD_RGB_SWAP_NONE;
	k_spin_unlock(&data->lock, key);
	if (undone) {
		/* Nothing is left to catch up: the frame's areas are this one's now. */
		memcpy(data->drawn, data->shown, data->shown_n * sizeof(data->shown[0]));
		data->drawn_n = data->shown_n;
		data->shown_n = 0U;
		LOG_WRN("no frame start: a swap undone");
	}
}

static int lcd_rgb_write(const struct device *dev, uint16_t x, uint16_t y,
			 const struct display_buffer_descriptor *desc, const void *buf)
{
	const struct lcd_rgb_config *config = dev->config;
	struct lcd_rgb_data *data = dev->data;
	const struct lcd_rgb_area area = {
		.x = x, .y = y, .w = desc->width, .h = desc->height,
	};
	k_spinlock_key_t key;

	if (!lcd_rgb_area_valid(config, x, y, desc, buf)) {
		return -EINVAL;
	}
	if (LCD_RGB_FB_NUM == 1U) {
		if (area.w != 0U && area.h != 0U) {
			lcd_rgb_copy_rows(config, data->fb[0], buf, desc->pitch * sizeof(uint16_t),
					  &area);
		}
		return 0;
	}

	k_mutex_lock(&data->fb_lock, K_FOREVER);
	lcd_rgb_swap_wait(dev);
	if (area.w != 0U && area.h != 0U) {
		/* No room to tell this area apart: catch up now, before it lands. */
		if (data->drawn_n == LCD_RGB_AREAS &&
		    lcd_rgb_area_band(data->drawn, data->drawn_n, &area) < 0) {
			lcd_rgb_catch_up(dev);
		}
		lcd_rgb_copy_rows(config, data->fb[data->front ^ 1U], buf,
				  desc->pitch * sizeof(uint16_t), &area);
		lcd_rgb_area_add(data->drawn, &data->drawn_n, &area);
	}
	if (!desc->frame_incomplete && data->drawn_n > 0U) {
		lcd_rgb_catch_up(dev);
		memcpy(data->shown, data->drawn, data->drawn_n * sizeof(data->drawn[0]));
		data->shown_n = data->drawn_n;
		data->drawn_n = 0U;
		k_sem_reset(&data->swapped);
		key = k_spin_lock(&data->lock);
		/* Linked now if the DMA is far from the loop's end, else at the next frame's start. */
		if (!lcd_rgb_arm(config, data)) {
			data->swap = LCD_RGB_SWAP_ASKED;
		}
		k_spin_unlock(&data->lock, key);
	}
	k_mutex_unlock(&data->fb_lock);
	return 0;
}

/* What the panel shows: with two buffers, once a frame ended is shown. */
static int lcd_rgb_read(const struct device *dev, uint16_t x, uint16_t y,
			const struct display_buffer_descriptor *desc, void *buf)
{
	const struct lcd_rgb_config *config = dev->config;
	struct lcd_rgb_data *data = dev->data;
	const size_t bpp = sizeof(uint16_t);
	const size_t stride = config->width * bpp;
	const uint8_t *src;
	uint8_t *dst = buf;

	if (!lcd_rgb_area_valid(config, x, y, desc, buf)) {
		return -EINVAL;
	}
	if (LCD_RGB_FB_NUM > 1U) {
		k_mutex_lock(&data->fb_lock, K_FOREVER);
		lcd_rgb_swap_wait(dev);
	}
	src = data->fb[data->front] + y * stride + x * bpp;
	for (uint16_t row = 0; row < desc->height; row++) {
		memcpy(dst, src, desc->width * bpp);
		src += stride;
		dst += desc->pitch * bpp;
	}
	if (LCD_RGB_FB_NUM > 1U) {
		k_mutex_unlock(&data->fb_lock);
	}
	return 0;
}

/* With two buffers none: a writer in the shown one would undo the swaps. */
static void *lcd_rgb_get_framebuffer(const struct device *dev)
{
	struct lcd_rgb_data *data = dev->data;

	return LCD_RGB_FB_NUM == 1U ? data->fb[0] : NULL;
}

static void lcd_rgb_get_capabilities(const struct device *dev,
				     struct display_capabilities *capabilities)
{
	const struct lcd_rgb_config *config = dev->config;

	memset(capabilities, 0, sizeof(*capabilities));
	capabilities->x_resolution = config->width;
	capabilities->y_resolution = config->height;
	capabilities->supported_pixel_formats = PIXEL_FORMAT_RGB_565;
	capabilities->current_pixel_format = PIXEL_FORMAT_RGB_565;
	capabilities->current_orientation = DISPLAY_ORIENTATION_NORMAL;
	capabilities->supported_events = DISPLAY_EVENT_VSYNC | DISPLAY_EVENT_FIFO_UNDERFLOW;
}

static int lcd_rgb_set_pixel_format(const struct device *dev,
				    const enum display_pixel_format pixel_format)
{
	ARG_UNUSED(dev);

	return pixel_format == PIXEL_FORMAT_RGB_565 ? 0 : -ENOTSUP;
}

static int lcd_rgb_set_orientation(const struct device *dev,
				   const enum display_orientation orientation)
{
	ARG_UNUSED(dev);

	return orientation == DISPLAY_ORIENTATION_NORMAL ? 0 : -ENOTSUP;
}

static int lcd_rgb_register_event_cb(const struct device *dev, display_event_cb_t cb,
				     void *user_data, uint32_t event_mask, bool in_isr,
				     uint32_t *out_reg_handle)
{
	struct lcd_rgb_data *data = dev->data;
	k_spinlock_key_t key;

	if (out_reg_handle == NULL) {
		return -EINVAL;
	}
	if (!in_isr) {
		return -ENOTSUP;
	}
	key = k_spin_lock(&data->lock);
	if (data->event_cb != NULL) {
		k_spin_unlock(&data->lock, key);
		return -EBUSY;
	}
	data->event_user_data = user_data;
	data->event_mask = event_mask;
	data->event_cb = cb;
	k_spin_unlock(&data->lock, key);
	*out_reg_handle = 1U;
	return 0;
}

static int lcd_rgb_unregister_event_cb(const struct device *dev, uint32_t reg_handle)
{
	struct lcd_rgb_data *data = dev->data;
	k_spinlock_key_t key;

	if (reg_handle != 1U) {
		return -EINVAL;
	}
	key = k_spin_lock(&data->lock);
	data->event_cb = NULL;
	data->event_mask = 0U;
	data->event_user_data = NULL;
	k_spin_unlock(&data->lock, key);
	return 0;
}

/*
 * A software reset leaves all of this running through the boot: the panel lit on whatever the DMA
 * finds while the memory it reads goes away, and the APLL on.
 */
static void lcd_rgb_shutdown(void)
{
	const struct device *dev = DEVICE_DT_INST_GET(0);
	const struct lcd_rgb_config *config = dev->config;

	(void)lcd_rgb_blanking_on(dev);
	lcd_ll_stop(lcd_rgb_hw());
	lcd_rgb_dma_reset(AXI_DMA_LL_GET_HW(0), config->dma_channel);
	if (config->apll) {
		(void)esp_clk_tree_enable_src(SOC_MOD_CLK_APLL, false);
	}
}

static DEVICE_API(display, lcd_rgb_api) = {
	.blanking_on = lcd_rgb_blanking_on,
	.blanking_off = lcd_rgb_blanking_off,
	.write = lcd_rgb_write,
	.read = lcd_rgb_read,
	.get_framebuffer = lcd_rgb_get_framebuffer,
	.get_capabilities = lcd_rgb_get_capabilities,
	.set_pixel_format = lcd_rgb_set_pixel_format,
	.set_orientation = lcd_rgb_set_orientation,
	.register_event_cb = lcd_rgb_register_event_cb,
	.unregister_event_cb = lcd_rgb_unregister_event_cb,
};

static int lcd_rgb_init(const struct device *dev)
{
	const struct lcd_rgb_config *config = dev->config;
	struct lcd_rgb_data *data = dev->data;
	int ret;

	/* Before anything can fail: a caller of the API waits on these. */
	k_sem_init(&data->blank_begun, 0, 1);
	if (LCD_RGB_FB_NUM > 1U) {
		k_sem_init(&data->swapped, 0, 1);
		k_mutex_init(&data->fb_lock);
	}
	if (config->backlight.port != NULL) {
		if (!gpio_is_ready_dt(&config->backlight)) {
			return -ENODEV;
		}
		ret = gpio_pin_configure_dt(&config->backlight, GPIO_OUTPUT_INACTIVE);
		if (ret != 0) {
			return ret;
		}
	}
	ret = pinctrl_apply_state(config->pcfg, PINCTRL_STATE_DEFAULT);
	if (ret != 0) {
		return ret;
	}

	data->fb_size = (size_t)config->width * config->height * sizeof(uint16_t);
	for (uint32_t b = 0; b < LCD_RGB_FB_NUM; b++) {
		dma_descriptor_align8_t *ring = lcd_rgb_ring(config, b);

		data->fb[b] = shared_multi_heap_aligned_alloc(SMH_REG_ATTR_EXTERNAL,
							      LCD_RGB_FB_ALIGN, data->fb_size);
		if (data->fb[b] == NULL) {
			LOG_ERR("no %zu bytes of PSRAM for framebuffer %u", data->fb_size, b);
			for (uint32_t k = 0; k < b; k++) {
				shared_multi_heap_free(data->fb[k]);
				data->fb[k] = NULL;
			}
			return -ENOMEM;
		}
		memset(data->fb[b], 0, data->fb_size);
		lcd_rgb_flush(data->fb[b], data->fb_size);

		for (size_t i = 0; i < config->desc_count; i++) {
			size_t at = i * LCD_RGB_DESC_BYTES;
			size_t n = MIN(data->fb_size - at, (size_t)LCD_RGB_DESC_BYTES);
			dma_descriptor_align8_t *d = &ring[i];

			memset(d, 0, sizeof(*d));
			d->buffer = data->fb[b] + at;
			d->dw0.size = n;
			d->dw0.length = n;
			d->dw0.owner = DMA_DESCRIPTOR_BUFFER_OWNER_DMA;
			d->dw0.suc_eof = i + 1U == config->desc_count;
			d->next = &ring[(i + 1U) % config->desc_count];
		}
	}

	ret = lcd_rgb_start(dev);
	if (ret != 0) {
		return ret;
	}
	return esp_register_shutdown_handler(lcd_rgb_shutdown) == ESP_OK ? 0 : -ENOMEM;
}

#define LCD_RGB_TIMING(n, prop) DT_PROP(DT_INST_CHILD(n, display_timings), prop)

#define LCD_RGB_FB_BYTES(n) (DT_INST_PROP(n, width) * DT_INST_PROP(n, height) * 2U)

#define LCD_RGB_DEVICE(n)                                                                          \
	BUILD_ASSERT(DT_INST_PROP(n, pixel_format) == PANEL_PIXEL_FORMAT_RGB_565,                  \
		     "RGB565 is what this driver scans out");                                      \
	PINCTRL_DT_INST_DEFINE(n);                                                                 \
	static dma_descriptor_align8_t lcd_rgb_desc_##n[LCD_RGB_FB_NUM *                          \
							DIV_ROUND_UP(LCD_RGB_FB_BYTES(n),          \
								     LCD_RGB_DESC_BYTES)];         \
	static struct lcd_rgb_data lcd_rgb_data_##n;                                               \
	static const struct lcd_rgb_config lcd_rgb_config_##n = {                                  \
		.pcfg = PINCTRL_DT_INST_DEV_CONFIG_GET(n),                                         \
		.backlight = GPIO_DT_SPEC_INST_GET_OR(n, backlight_gpios, {0}),                    \
		.desc = lcd_rgb_desc_##n,                                                          \
		.desc_count = ARRAY_SIZE(lcd_rgb_desc_##n) / LCD_RGB_FB_NUM,                       \
		.pclk_hz = LCD_RGB_TIMING(n, clock_frequency),                                     \
		.apll = DT_INST_ENUM_HAS_VALUE(n, clock_source, apll),                             \
		.width = DT_INST_PROP(n, width),                                                   \
		.height = DT_INST_PROP(n, height),                                                 \
		.hsync_len = LCD_RGB_TIMING(n, hsync_len),                                         \
		.hback_porch = LCD_RGB_TIMING(n, hback_porch),                                     \
		.hfront_porch = LCD_RGB_TIMING(n, hfront_porch),                                   \
		.vsync_len = LCD_RGB_TIMING(n, vsync_len),                                         \
		.vback_porch = LCD_RGB_TIMING(n, vback_porch),                                     \
		.vfront_porch = LCD_RGB_TIMING(n, vfront_porch),                                   \
		.hsync_active = LCD_RGB_TIMING(n, hsync_active),                                   \
		.vsync_active = LCD_RGB_TIMING(n, vsync_active),                                   \
		.de_active = LCD_RGB_TIMING(n, de_active),                                         \
		.pixelclk_active = LCD_RGB_TIMING(n, pixelclk_active),                             \
		.dma_channel = DT_INST_PROP(n, dma_channel),                                       \
		.irq_source = DT_IRQ_BY_IDX(DT_INST_PARENT(n), 0, irq),                            \
		.irq_priority = DT_IRQ_BY_IDX(DT_INST_PARENT(n), 0, priority),                     \
		.irq_flags = DT_IRQ_BY_IDX(DT_INST_PARENT(n), 0, flags),                           \
	};                                                                                         \
	DEVICE_DT_INST_DEFINE(n, lcd_rgb_init, NULL, &lcd_rgb_data_##n, &lcd_rgb_config_##n,       \
			      POST_KERNEL, CONFIG_DISPLAY_ESP32_LCD_RGB_INIT_PRIORITY,             \
			      &lcd_rgb_api);

BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) <= 1, "LCD-CAM has one LCD side");

DT_INST_FOREACH_STATUS_OKAY(LCD_RGB_DEVICE)
