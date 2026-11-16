/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT ti_tca8418

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(input_tca8418, CONFIG_INPUT_LOG_LEVEL);

#define TCA8418_CFG         0x01U
#define TCA8418_INT_STAT    0x02U
#define TCA8418_KEY_LCK_EC  0x03U
#define TCA8418_KEY_EVENT_A 0x04U
#define TCA8418_KP_GPIO1    0x1DU
#define TCA8418_KP_GPIO2    0x1EU
#define TCA8418_KP_GPIO3    0x1FU

#define TCA8418_CFG_INT_CFG BIT(4)
#define TCA8418_CFG_KE_IEN  BIT(0)

#define TCA8418_INT_STAT_ALL 0x1FU

#define TCA8418_KEY_LCK_EC_COUNT 0x0FU

#define TCA8418_EVENT_PRESS BIT(7)
#define TCA8418_EVENT_CODE  0x7FU

#define TCA8418_MAX_ROWS   8U
#define TCA8418_MAX_COLS   10U
#define TCA8418_FIFO_DEPTH 10U

/* Reset pulse width and recovery time */
#define TCA8418_RESET_US 120U

struct tca8418_config {
	struct i2c_dt_spec i2c;
	struct gpio_dt_spec int_gpio;
	struct gpio_dt_spec reset_gpio;
	uint8_t row_size;
	uint8_t col_size;
	uint32_t poll_period_ms;
};

struct tca8418_data {
	const struct device *dev;
	struct k_work_delayable work;
	struct gpio_callback int_gpio_cb;
};

static void tca8418_report(const struct device *dev, uint8_t event)
{
	const struct tca8418_config *config = dev->config;
	uint8_t code = event & TCA8418_EVENT_CODE;
	uint8_t row;
	uint8_t col;

	/* Keypad events are numbered row * 10 + col + 1; GPI events follow from 97 */
	if ((code == 0U) || (code > (TCA8418_MAX_ROWS * TCA8418_MAX_COLS))) {
		return;
	}

	row = (code - 1U) / TCA8418_MAX_COLS;
	col = (code - 1U) % TCA8418_MAX_COLS;

	if ((row >= config->row_size) || (col >= config->col_size)) {
		return;
	}

	input_report_abs(dev, INPUT_ABS_X, col, false, K_FOREVER);
	input_report_abs(dev, INPUT_ABS_Y, row, false, K_FOREVER);
	input_report_key(dev, INPUT_BTN_TOUCH, (event & TCA8418_EVENT_PRESS) != 0U, true,
			 K_FOREVER);
}

static int tca8418_read_events(const struct device *dev)
{
	const struct tca8418_config *config = dev->config;
	uint8_t count;
	uint8_t event;
	int ret;

	ret = i2c_reg_read_byte_dt(&config->i2c, TCA8418_KEY_LCK_EC, &count);
	if (ret < 0) {
		return ret;
	}

	if ((count & TCA8418_KEY_LCK_EC_COUNT) == 0U) {
		return 0;
	}

	/* Each read pops one event; a zero event means the FIFO is empty */
	for (uint8_t i = 0U; i < TCA8418_FIFO_DEPTH; i++) {
		ret = i2c_reg_read_byte_dt(&config->i2c, TCA8418_KEY_EVENT_A, &event);
		if (ret < 0) {
			return ret;
		}

		if (event == 0U) {
			break;
		}

		tca8418_report(dev, event);
	}

	return i2c_reg_write_byte_dt(&config->i2c, TCA8418_INT_STAT, TCA8418_INT_STAT_ALL);
}

static void tca8418_work_handler(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct tca8418_data *data = CONTAINER_OF(dwork, struct tca8418_data, work);
	const struct device *dev = data->dev;
	const struct tca8418_config *config = dev->config;
	int ret;

	ret = tca8418_read_events(dev);
	if (ret < 0) {
		LOG_ERR("Failed to read key events: %d", ret);
	}

	if (config->int_gpio.port == NULL) {
		k_work_reschedule(dwork, K_MSEC(config->poll_period_ms));
	}
}

static void tca8418_isr(const struct device *port, struct gpio_callback *cb, uint32_t pins)
{
	struct tca8418_data *data = CONTAINER_OF(cb, struct tca8418_data, int_gpio_cb);

	ARG_UNUSED(port);
	ARG_UNUSED(pins);

	k_work_reschedule(&data->work, K_NO_WAIT);
}

static int tca8418_reset(const struct device *dev)
{
	const struct tca8418_config *config = dev->config;
	int ret;

	if (config->reset_gpio.port == NULL) {
		return 0;
	}

	if (!gpio_is_ready_dt(&config->reset_gpio)) {
		LOG_ERR_DEVICE_NOT_READY(config->reset_gpio.port);
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(&config->reset_gpio, GPIO_OUTPUT_ACTIVE);
	if (ret < 0) {
		LOG_ERR("Failed to configure reset GPIO: %d", ret);
		return ret;
	}

	k_busy_wait(TCA8418_RESET_US);

	ret = gpio_pin_set_dt(&config->reset_gpio, 0);
	if (ret < 0) {
		LOG_ERR("Failed to release reset: %d", ret);
		return ret;
	}

	k_busy_wait(TCA8418_RESET_US);

	return 0;
}

static int tca8418_init_chip(const struct device *dev)
{
	const struct tca8418_config *config = dev->config;
	uint16_t cols = (uint16_t)BIT_MASK(config->col_size);
	const uint8_t seq[][2] = {
		{TCA8418_KP_GPIO1, (uint8_t)BIT_MASK(config->row_size)},
		{TCA8418_KP_GPIO2, (uint8_t)(cols & 0xFFU)},
		{TCA8418_KP_GPIO3, (uint8_t)(cols >> 8)},
		{TCA8418_CFG, TCA8418_CFG_INT_CFG | TCA8418_CFG_KE_IEN},
	};
	int ret;

	for (uint8_t i = 0U; i < ARRAY_SIZE(seq); i++) {
		ret = i2c_reg_write_byte_dt(&config->i2c, seq[i][0], seq[i][1]);
		if (ret < 0) {
			LOG_ERR("Failed to write register 0x%02x: %d", seq[i][0], ret);
			return ret;
		}
	}

	return 0;
}

static int tca8418_init_interrupt(const struct device *dev)
{
	const struct tca8418_config *config = dev->config;
	struct tca8418_data *data = dev->data;
	int ret;

	if (config->int_gpio.port == NULL) {
		return 0;
	}

	if (!gpio_is_ready_dt(&config->int_gpio)) {
		LOG_ERR_DEVICE_NOT_READY(config->int_gpio.port);
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(&config->int_gpio, GPIO_INPUT);
	if (ret < 0) {
		LOG_ERR("Failed to configure interrupt GPIO: %d", ret);
		return ret;
	}

	gpio_init_callback(&data->int_gpio_cb, tca8418_isr, BIT(config->int_gpio.pin));
	ret = gpio_add_callback_dt(&config->int_gpio, &data->int_gpio_cb);
	if (ret < 0) {
		LOG_ERR("Failed to add GPIO callback: %d", ret);
		return ret;
	}

	ret = gpio_pin_interrupt_configure_dt(&config->int_gpio, GPIO_INT_EDGE_TO_ACTIVE);
	if (ret < 0) {
		LOG_ERR("Failed to configure GPIO interrupt: %d", ret);
		return ret;
	}

	return 0;
}

static int tca8418_init(const struct device *dev)
{
	const struct tca8418_config *config = dev->config;
	struct tca8418_data *data = dev->data;
	int ret;

	if (!i2c_is_ready_dt(&config->i2c)) {
		LOG_ERR_DEVICE_NOT_READY(config->i2c.bus);
		return -ENODEV;
	}

	data->dev = dev;
	k_work_init_delayable(&data->work, tca8418_work_handler);

	ret = tca8418_reset(dev);
	if (ret < 0) {
		return ret;
	}

	ret = tca8418_init_chip(dev);
	if (ret < 0) {
		return ret;
	}

	ret = tca8418_init_interrupt(dev);
	if (ret < 0) {
		return ret;
	}

	/* Drains events queued before the interrupt was set up, or starts polling */
	k_work_schedule(&data->work, K_NO_WAIT);

	return 0;
}

#define TCA8418_INIT(n)                                                                            \
	BUILD_ASSERT(IN_RANGE(DT_INST_PROP(n, row_size), 1, TCA8418_MAX_ROWS),                     \
		     "row-size must be 1 to 8");                                                   \
	BUILD_ASSERT(IN_RANGE(DT_INST_PROP(n, col_size), 1, TCA8418_MAX_COLS),                     \
		     "col-size must be 1 to 10");                                                  \
                                                                                                   \
	static const struct tca8418_config tca8418_config_##n = {                                  \
		.i2c = I2C_DT_SPEC_INST_GET(n),                                                    \
		.int_gpio = GPIO_DT_SPEC_INST_GET_OR(n, int_gpios, {0}),                           \
		.reset_gpio = GPIO_DT_SPEC_INST_GET_OR(n, reset_gpios, {0}),                       \
		.row_size = DT_INST_PROP(n, row_size),                                             \
		.col_size = DT_INST_PROP(n, col_size),                                             \
		.poll_period_ms = DT_INST_PROP(n, poll_period_ms),                                 \
	};                                                                                         \
                                                                                                   \
	static struct tca8418_data tca8418_data_##n;                                               \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(n, tca8418_init, NULL, &tca8418_data_##n, &tca8418_config_##n,       \
			      POST_KERNEL, CONFIG_INPUT_INIT_PRIORITY, NULL);

DT_INST_FOREACH_STATUS_OKAY(TCA8418_INIT)
