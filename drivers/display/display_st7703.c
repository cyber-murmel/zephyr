/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT sitronix_st7703

#include <string.h>

#include <zephyr/drivers/display.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/mipi_dsi.h>
#include <zephyr/display/mipi_display.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(st7703, CONFIG_DISPLAY_LOG_LEVEL);

#define ST7703_RESET_RELEASE_MS 5U
#define ST7703_RESET_PULSE_MS   10U
#define ST7703_RESET_WAIT_MS    120U
#define ST7703_SLPOUT_WAIT_MS   250U
#define ST7703_DISPON_WAIT_MS   50U

struct st7703_cmd {
	uint8_t cmd;
	uint8_t len;
	const uint8_t *data;
};

#define ST7703_CMD(_cmd, ...)                                                                      \
	{                                                                                          \
		.cmd = (_cmd), .len = sizeof((const uint8_t[]){__VA_ARGS__}),                      \
		.data = (const uint8_t[]){__VA_ARGS__},                                            \
	}

/* 720x720 panel of the WHY2025 badge (black bezel), 2 data lanes */
static const struct st7703_cmd st7703_init_cmds[] = {
	/* Enable the extension commands */
	ST7703_CMD(0xB9, 0xF1, 0x12, 0x83),
	ST7703_CMD(0xB1, 0x00, 0x00, 0x00, 0xDA, 0x80),
	ST7703_CMD(0xB2, 0x3C, 0x02, 0x30),
	ST7703_CMD(0xB3, 0x10, 0x10, 0x28, 0x28, 0x03, 0xFF, 0x00, 0x00, 0x00, 0x00),
	ST7703_CMD(0xB4, 0x80),
	ST7703_CMD(0xB5, 0x0A, 0x0A),
	ST7703_CMD(0xB6, 0x97, 0x97),
	ST7703_CMD(0xB8, 0x26, 0x22, 0xF0, 0x63),
	/* MIPI interface: 2 lanes */
	ST7703_CMD(0xBA, 0x31, 0x81, 0x05, 0xF9, 0x0E, 0x0E, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00,
		   0x00, 0x00, 0x44, 0x25, 0x00, 0x90, 0x0A, 0x00, 0x00, 0x01, 0x4F, 0x01, 0x00,
		   0x00, 0x37),
	ST7703_CMD(0xBC, 0x47),
	ST7703_CMD(0xBF, 0x02, 0x11, 0x00),
	ST7703_CMD(0xC0, 0x73, 0x73, 0x50, 0x50, 0x00, 0x00, 0x12, 0x70, 0x00),
	ST7703_CMD(0xC1, 0x43, 0x00, 0x32, 0x32, 0x77, 0xC1, 0xFF, 0xFF, 0xCC, 0xCC, 0x77, 0x77),
	ST7703_CMD(0xC6, 0x82, 0x00, 0xBF, 0xFF, 0x00, 0xFF),
	ST7703_CMD(0xC7, 0xB8, 0x00, 0x0A, 0x00, 0x00, 0x00),
	ST7703_CMD(0xC8, 0x10, 0x40, 0x1E, 0x02),
	ST7703_CMD(0xCC, 0x0B),
	/* Gamma */
	ST7703_CMD(0xE0, 0x00, 0x0B, 0x10, 0x2C, 0x3D, 0x3F, 0x42, 0x3A, 0x07, 0x0D, 0x0F, 0x13,
		   0x15, 0x13, 0x14, 0x0F, 0x16, 0x00, 0x0B, 0x10, 0x2C, 0x3D, 0x3F, 0x42, 0x3A,
		   0x07, 0x0D, 0x0F, 0x13, 0x15, 0x13, 0x14, 0x0F, 0x16),
	ST7703_CMD(0xE3, 0x07, 0x07, 0x0B, 0x0B, 0x0B, 0x0B, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x00,
		   0xC0, 0x10),
	/* Gate in panel timing */
	ST7703_CMD(0xE9, 0xC8, 0x10, 0x0A, 0x00, 0x00, 0x80, 0x81, 0x12, 0x31, 0x23, 0x4F, 0x86,
		   0xA0, 0x00, 0x47, 0x08, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C,
		   0x00, 0x00, 0x00, 0x98, 0x02, 0x8B, 0xAF, 0x46, 0x02, 0x88, 0x88, 0x88, 0x88,
		   0x88, 0x98, 0x13, 0x8B, 0xAF, 0x57, 0x13, 0x88, 0x88, 0x88, 0x88, 0x88, 0x00,
		   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00),
	ST7703_CMD(0xEA, 0x97, 0x0C, 0x09, 0x09, 0x09, 0x78, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		   0x9F, 0x31, 0x8B, 0xA8, 0x31, 0x75, 0x88, 0x88, 0x88, 0x88, 0x88, 0x9F, 0x20,
		   0x8B, 0xA8, 0x20, 0x64, 0x88, 0x88, 0x88, 0x88, 0x88, 0x23, 0x00, 0x00, 0x02,
		   0x62, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		   0x00, 0x00, 0x00, 0x40, 0x80, 0x81, 0x00, 0x00, 0x00, 0x00),
	ST7703_CMD(0xEF, 0xFF, 0xFF, 0x01),
};

struct st7703_config {
	const struct device *mipi_dsi;
	const struct gpio_dt_spec reset_gpio;
	const struct gpio_dt_spec bl_gpio;
	uint8_t num_of_lanes;
	uint8_t pixel_format;
	uint16_t panel_width;
	uint16_t panel_height;
	uint8_t channel;
	uint8_t madctl;
	uint32_t hbp;
	uint32_t hsync;
	uint32_t hfp;
	uint32_t vbp;
	uint32_t vsync;
	uint32_t vfp;
};

static int st7703_dcs_write(const struct device *dev, uint8_t cmd, const uint8_t *data, size_t len)
{
	const struct st7703_config *config = dev->config;
	ssize_t ret;

	ret = mipi_dsi_dcs_write(config->mipi_dsi, config->channel, cmd, data, len);
	if (ret < 0) {
		LOG_ERR("Command 0x%02x failed (%d)", cmd, (int)ret);
		return (int)ret;
	}

	return 0;
}

static int st7703_blanking_off(const struct device *dev)
{
	const struct st7703_config *config = dev->config;

	if (config->bl_gpio.port != NULL) {
		return gpio_pin_set_dt(&config->bl_gpio, 1);
	}

	return 0;
}

static int st7703_blanking_on(const struct device *dev)
{
	const struct st7703_config *config = dev->config;

	if (config->bl_gpio.port != NULL) {
		return gpio_pin_set_dt(&config->bl_gpio, 0);
	}

	return 0;
}

static enum display_pixel_format st7703_display_format(uint8_t pixfmt)
{
	return (pixfmt == MIPI_DSI_PIXFMT_RGB565) ? PIXEL_FORMAT_RGB_565 : PIXEL_FORMAT_RGB_888;
}

static void st7703_get_capabilities(const struct device *dev,
				    struct display_capabilities *capabilities)
{
	const struct st7703_config *config = dev->config;
	enum display_pixel_format format = st7703_display_format(config->pixel_format);

	memset(capabilities, 0, sizeof(*capabilities));
	capabilities->x_resolution = config->panel_width;
	capabilities->y_resolution = config->panel_height;
	capabilities->supported_pixel_formats = format;
	capabilities->current_pixel_format = format;
	capabilities->current_orientation = DISPLAY_ORIENTATION_NORMAL;
}

static int st7703_set_pixel_format(const struct device *dev,
				   const enum display_pixel_format pixel_format)
{
	const struct st7703_config *config = dev->config;

	return (pixel_format == st7703_display_format(config->pixel_format)) ? 0 : -ENOTSUP;
}

static int st7703_set_orientation(const struct device *dev,
				  const enum display_orientation orientation)
{
	ARG_UNUSED(dev);

	return (orientation == DISPLAY_ORIENTATION_NORMAL) ? 0 : -ENOTSUP;
}

static DEVICE_API(display, st7703_api) = {
	.blanking_on = st7703_blanking_on,
	.blanking_off = st7703_blanking_off,
	.get_capabilities = st7703_get_capabilities,
	.set_pixel_format = st7703_set_pixel_format,
	.set_orientation = st7703_set_orientation,
};

static int st7703_reset(const struct device *dev)
{
	const struct st7703_config *config = dev->config;
	int ret;

	if (config->reset_gpio.port == NULL) {
		ret = st7703_dcs_write(dev, MIPI_DCS_SOFT_RESET, NULL, 0);
		if (ret < 0) {
			return ret;
		}
		k_msleep(ST7703_RESET_WAIT_MS);
		return 0;
	}

	if (!gpio_is_ready_dt(&config->reset_gpio)) {
		LOG_ERR_DEVICE_NOT_READY(config->reset_gpio.port);
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(&config->reset_gpio, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		LOG_ERR("Failed to configure reset GPIO (%d)", ret);
		return ret;
	}
	k_msleep(ST7703_RESET_RELEASE_MS);

	ret = gpio_pin_set_dt(&config->reset_gpio, 1);
	if (ret < 0) {
		return ret;
	}
	k_msleep(ST7703_RESET_PULSE_MS);

	ret = gpio_pin_set_dt(&config->reset_gpio, 0);
	if (ret < 0) {
		return ret;
	}
	k_msleep(ST7703_RESET_WAIT_MS);

	return 0;
}

static int st7703_init(const struct device *dev)
{
	const struct st7703_config *config = dev->config;
	struct mipi_dsi_device mdev = {0};
	uint8_t param;
	int ret;

	if (!device_is_ready(config->mipi_dsi)) {
		LOG_ERR("MIPI DSI host not ready");
		return -ENODEV;
	}

	if (config->num_of_lanes != 2U) {
		LOG_ERR("Unsupported lane count %u, the panel table is for 2",
			config->num_of_lanes);
		return -ENOTSUP;
	}

	ret = st7703_reset(dev);
	if (ret < 0) {
		return ret;
	}

	if (config->bl_gpio.port != NULL) {
		ret = gpio_pin_configure_dt(&config->bl_gpio, GPIO_OUTPUT_INACTIVE);
		if (ret < 0) {
			LOG_ERR("Failed to configure backlight GPIO (%d)", ret);
			return ret;
		}
	}

	ret = st7703_dcs_write(dev, MIPI_DCS_SET_ADDRESS_MODE, &config->madctl,
			       sizeof(config->madctl));
	if (ret < 0) {
		return ret;
	}

	param = (config->pixel_format == MIPI_DSI_PIXFMT_RGB565) ? MIPI_DCS_PIXEL_FORMAT_16BIT
								  : MIPI_DCS_PIXEL_FORMAT_24BIT;
	ret = st7703_dcs_write(dev, MIPI_DCS_SET_PIXEL_FORMAT, &param, sizeof(param));
	if (ret < 0) {
		return ret;
	}

	for (size_t i = 0; i < ARRAY_SIZE(st7703_init_cmds); i++) {
		ret = st7703_dcs_write(dev, st7703_init_cmds[i].cmd, st7703_init_cmds[i].data,
				       st7703_init_cmds[i].len);
		if (ret < 0) {
			return ret;
		}
	}

	ret = st7703_dcs_write(dev, MIPI_DCS_EXIT_SLEEP_MODE, NULL, 0);
	if (ret < 0) {
		return ret;
	}
	k_msleep(ST7703_SLPOUT_WAIT_MS);

	ret = st7703_dcs_write(dev, MIPI_DCS_SET_DISPLAY_ON, NULL, 0);
	if (ret < 0) {
		return ret;
	}
	k_msleep(ST7703_DISPON_WAIT_MS);

	mdev.pixfmt = config->pixel_format;
	mdev.data_lanes = config->num_of_lanes;
	mdev.mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST;
	mdev.timings.hactive = config->panel_width;
	mdev.timings.hfp = config->hfp;
	mdev.timings.hbp = config->hbp;
	mdev.timings.hsync = config->hsync;
	mdev.timings.vactive = config->panel_height;
	mdev.timings.vfp = config->vfp;
	mdev.timings.vbp = config->vbp;
	mdev.timings.vsync = config->vsync;

	ret = mipi_dsi_attach(config->mipi_dsi, config->channel, &mdev);
	if (ret < 0) {
		LOG_ERR("Failed to attach to MIPI DSI host (%d)", ret);
		return ret;
	}

	return 0;
}

#define ST7703_TIMING(id, prop) DT_PROP(DT_INST_CHILD(id, display_timings), prop)

#define ST7703_PANEL(id)                                                                           \
	static const struct st7703_config st7703_config_##id = {                                   \
		.mipi_dsi = DEVICE_DT_GET(DT_INST_BUS(id)),                                        \
		.reset_gpio = GPIO_DT_SPEC_INST_GET_OR(id, reset_gpios, {0}),                      \
		.bl_gpio = GPIO_DT_SPEC_INST_GET_OR(id, bl_gpios, {0}),                            \
		.num_of_lanes = DT_INST_PROP_BY_IDX(id, data_lanes, 0),                            \
		.pixel_format = DT_INST_PROP(id, pixel_format),                                    \
		.panel_width = DT_INST_PROP(id, width),                                            \
		.panel_height = DT_INST_PROP(id, height),                                          \
		.channel = DT_INST_REG_ADDR(id),                                                   \
		.madctl = DT_INST_PROP(id, madctl),                                                \
		.hbp = ST7703_TIMING(id, hback_porch),                                             \
		.hsync = ST7703_TIMING(id, hsync_len),                                             \
		.hfp = ST7703_TIMING(id, hfront_porch),                                            \
		.vbp = ST7703_TIMING(id, vback_porch),                                             \
		.vsync = ST7703_TIMING(id, vsync_len),                                             \
		.vfp = ST7703_TIMING(id, vfront_porch),                                            \
	};                                                                                         \
	DEVICE_DT_INST_DEFINE(id, st7703_init, NULL, NULL, &st7703_config_##id, POST_KERNEL,       \
			      CONFIG_DISPLAY_ST7703_INIT_PRIORITY, &st7703_api);

DT_INST_FOREACH_STATUS_OKAY(ST7703_PANEL)
