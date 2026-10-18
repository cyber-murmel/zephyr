/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT newvision_nv3007

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/display/mipi_display.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/mipi_dbi.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/pm/device.h>
#include <zephyr/sys/byteorder.h>

LOG_MODULE_REGISTER(display_nv3007, CONFIG_DISPLAY_LOG_LEVEL);

/* Command page select: 0xA5 opens the vendor registers, 0x00 closes them */
#define NV3007_CMD_PAGE    0xFFU
#define NV3007_PAGE_VENDOR 0xA5U
#define NV3007_PAGE_USER   0x00U

#define NV3007_PIXEL_SIZE 2U

#define NV3007_RESET_PULSE_MS 10U
#define NV3007_RESET_WAIT_MS  120U
#define NV3007_SLPOUT_WAIT_MS 120U
#define NV3007_SLPIN_WAIT_MS  120U

struct nv3007_reg {
	uint8_t cmd;
	uint8_t len;
	uint8_t data[4];
};

/* Power, gamma, timing, gate and source settings for a 142x428 panel */
static const struct nv3007_reg nv3007_init_regs[] = {
	{0x9A, 1, {0x08}},
	{0x9B, 1, {0x08}},
	{0x9C, 1, {0xB0}},
	{0x9D, 1, {0x16}},
	{0x9E, 1, {0xC4}},
	{0x8F, 2, {0x55, 0x04}},
	{0x84, 1, {0x90}},
	{0x83, 1, {0x7B}},
	{0x85, 1, {0x33}},
	{0x60, 1, {0x00}},
	{0x70, 1, {0x00}},
	{0x61, 1, {0x02}},
	{0x71, 1, {0x02}},
	{0x62, 1, {0x04}},
	{0x72, 1, {0x04}},
	{0x6C, 1, {0x29}},
	{0x7C, 1, {0x29}},
	{0x6D, 1, {0x31}},
	{0x7D, 1, {0x31}},
	{0x6E, 1, {0x0F}},
	{0x7E, 1, {0x0F}},
	{0x66, 1, {0x21}},
	{0x76, 1, {0x21}},
	{0x68, 1, {0x3A}},
	{0x78, 1, {0x3A}},
	{0x63, 1, {0x07}},
	{0x73, 1, {0x07}},
	{0x64, 1, {0x05}},
	{0x74, 1, {0x05}},
	{0x65, 1, {0x02}},
	{0x75, 1, {0x02}},
	{0x67, 1, {0x23}},
	{0x77, 1, {0x23}},
	{0x69, 1, {0x08}},
	{0x79, 1, {0x08}},
	{0x6A, 1, {0x13}},
	{0x7A, 1, {0x13}},
	{0x6B, 1, {0x13}},
	{0x7B, 1, {0x13}},
	{0x6F, 1, {0x00}},
	{0x7F, 1, {0x00}},
	{0x50, 1, {0x00}},
	{0x52, 1, {0xD6}},
	{0x53, 1, {0x08}},
	{0x54, 1, {0x08}},
	{0x55, 1, {0x1E}},
	{0x56, 1, {0x1C}},
	{0xA0, 3, {0x2B, 0x24, 0x00}},
	{0xA1, 1, {0x87}},
	{0xA2, 1, {0x86}},
	{0xA5, 1, {0x00}},
	{0xA6, 1, {0x00}},
	{0xA7, 1, {0x00}},
	{0xA8, 1, {0x36}},
	{0xA9, 1, {0x7E}},
	{0xAA, 1, {0x7E}},
	{0xB9, 1, {0x85}},
	{0xBA, 1, {0x84}},
	{0xBB, 1, {0x83}},
	{0xBC, 1, {0x82}},
	{0xBD, 1, {0x81}},
	{0xBE, 1, {0x80}},
	{0xBF, 1, {0x01}},
	{0xC0, 1, {0x02}},
	{0xC1, 1, {0x00}},
	{0xC2, 1, {0x00}},
	{0xC3, 1, {0x00}},
	{0xC4, 1, {0x33}},
	{0xC5, 1, {0x7E}},
	{0xC6, 1, {0x7E}},
	{0xC8, 2, {0x33, 0x33}},
	{0xC9, 1, {0x68}},
	{0xCA, 1, {0x69}},
	{0xCB, 1, {0x6A}},
	{0xCC, 1, {0x6B}},
	{0xCD, 2, {0x33, 0x33}},
	{0xCE, 1, {0x6C}},
	{0xCF, 1, {0x6D}},
	{0xD0, 1, {0x6E}},
	{0xD1, 1, {0x6F}},
	{0xAB, 2, {0x03, 0x67}},
	{0xAC, 2, {0x03, 0x6B}},
	{0xAD, 2, {0x03, 0x68}},
	{0xAE, 2, {0x03, 0x6C}},
	{0xB3, 1, {0x00}},
	{0xB4, 1, {0x00}},
	{0xB5, 1, {0x00}},
	{0xB6, 1, {0x32}},
	{0xB7, 1, {0x7E}},
	{0xB8, 1, {0x7E}},
	{0xE0, 1, {0x00}},
	{0xE1, 2, {0x03, 0x0F}},
	{0xE2, 1, {0x04}},
	{0xE3, 1, {0x01}},
	{0xE4, 1, {0x0E}},
	{0xE5, 1, {0x01}},
	{0xE6, 1, {0x19}},
	{0xE7, 1, {0x10}},
	{0xE8, 1, {0x10}},
	{0xEA, 1, {0x12}},
	{0xEB, 1, {0xD0}},
	{0xEC, 1, {0x04}},
	{0xED, 1, {0x07}},
	{0xEE, 1, {0x07}},
	{0xEF, 1, {0x09}},
	{0xF0, 1, {0xD0}},
	{0xF1, 1, {0x0E}},
	{0xF9, 1, {0x17}},
	{0xF2, 4, {0x2C, 0x1B, 0x0B, 0x20}},
	{0xE9, 1, {0x29}},
	{0xEC, 1, {0x04}},
	{MIPI_DCS_SET_TEAR_SCANLINE, 2, {0x00, 0x10}},
	{0x46, 1, {0x10}},
};

struct nv3007_config {
	const struct device *mipi_dbi;
	const struct mipi_dbi_config dbi_config;
	uint16_t width;
	uint16_t height;
	uint16_t x_offset;
	uint16_t y_offset;
	uint8_t madctl;
	bool color_invert;
	uint8_t te_mode;
	uint32_t te_delay;
};

static int nv3007_send_cmd(const struct device *dev, uint8_t cmd, const uint8_t *data, size_t len)
{
	const struct nv3007_config *config = dev->config;

	return mipi_dbi_command_write(config->mipi_dbi, &config->dbi_config, cmd, data, len);
}

static void nv3007_sleep_ms(uint32_t ms)
{
	if (k_can_yield()) {
		k_msleep(ms);
	} else {
		k_busy_wait(ms * USEC_PER_MSEC);
	}
}

static int nv3007_set_window(const struct device *dev, uint16_t x, uint16_t y, uint16_t width,
			     uint16_t height)
{
	const struct nv3007_config *config = dev->config;
	uint16_t addr[2];
	int ret;

	x += config->x_offset;
	y += config->y_offset;

	addr[0] = sys_cpu_to_be16(x);
	addr[1] = sys_cpu_to_be16(x + width - 1U);
	ret = nv3007_send_cmd(dev, MIPI_DCS_SET_COLUMN_ADDRESS, (uint8_t *)addr, sizeof(addr));
	if (ret < 0) {
		return ret;
	}

	addr[0] = sys_cpu_to_be16(y);
	addr[1] = sys_cpu_to_be16(y + height - 1U);
	return nv3007_send_cmd(dev, MIPI_DCS_SET_PAGE_ADDRESS, (uint8_t *)addr, sizeof(addr));
}

static int nv3007_write(const struct device *dev, const uint16_t x, const uint16_t y,
			const struct display_buffer_descriptor *desc, const void *buf)
{
	const struct nv3007_config *config = dev->config;
	struct display_buffer_descriptor mipi_desc = *desc;
	int ret;

	if ((x + desc->width > config->width) || (y + desc->height > config->height)) {
		return -EINVAL;
	}

	if (desc->pitch != desc->width) {
		LOG_ERR("Pitch other than width is not supported");
		return -ENOTSUP;
	}

	ret = nv3007_set_window(dev, x, y, desc->width, desc->height);
	if (ret < 0) {
		return ret;
	}

	ret = nv3007_send_cmd(dev, MIPI_DCS_WRITE_MEMORY_START, NULL, 0);
	if (ret < 0) {
		return ret;
	}

	mipi_desc.buf_size = (uint32_t)desc->width * desc->height * NV3007_PIXEL_SIZE;

	return mipi_dbi_write_display(config->mipi_dbi, &config->dbi_config, buf, &mipi_desc,
				      PIXEL_FORMAT_RGB_565X);
}

static int nv3007_blanking_on(const struct device *dev)
{
	return nv3007_send_cmd(dev, MIPI_DCS_SET_DISPLAY_OFF, NULL, 0);
}

static int nv3007_blanking_off(const struct device *dev)
{
	return nv3007_send_cmd(dev, MIPI_DCS_SET_DISPLAY_ON, NULL, 0);
}

static void nv3007_get_capabilities(const struct device *dev,
				    struct display_capabilities *capabilities)
{
	const struct nv3007_config *config = dev->config;

	memset(capabilities, 0, sizeof(*capabilities));
	capabilities->x_resolution = config->width;
	capabilities->y_resolution = config->height;
	capabilities->supported_pixel_formats = PIXEL_FORMAT_RGB_565X;
	capabilities->current_pixel_format = PIXEL_FORMAT_RGB_565X;
	capabilities->current_orientation = DISPLAY_ORIENTATION_NORMAL;
}

static int nv3007_set_pixel_format(const struct device *dev,
				   const enum display_pixel_format pixel_format)
{
	ARG_UNUSED(dev);

	return (pixel_format == PIXEL_FORMAT_RGB_565X) ? 0 : -ENOTSUP;
}

static int nv3007_set_orientation(const struct device *dev,
				  const enum display_orientation orientation)
{
	ARG_UNUSED(dev);

	return (orientation == DISPLAY_ORIENTATION_NORMAL) ? 0 : -ENOTSUP;
}

static int nv3007_configure(const struct device *dev)
{
	const struct nv3007_config *config = dev->config;
	uint8_t param;
	int ret;

	param = NV3007_PAGE_VENDOR;
	ret = nv3007_send_cmd(dev, NV3007_CMD_PAGE, &param, sizeof(param));
	if (ret < 0) {
		return ret;
	}

	for (size_t i = 0; i < ARRAY_SIZE(nv3007_init_regs); i++) {
		const struct nv3007_reg *reg = &nv3007_init_regs[i];

		ret = nv3007_send_cmd(dev, reg->cmd, reg->data, reg->len);
		if (ret < 0) {
			return ret;
		}
	}

	param = NV3007_PAGE_USER;
	ret = nv3007_send_cmd(dev, NV3007_CMD_PAGE, &param, sizeof(param));
	if (ret < 0) {
		return ret;
	}

	param = MIPI_DCS_PIXEL_FORMAT_16BIT;
	ret = nv3007_send_cmd(dev, MIPI_DCS_SET_PIXEL_FORMAT, &param, sizeof(param));
	if (ret < 0) {
		return ret;
	}

	ret = nv3007_send_cmd(dev, MIPI_DCS_SET_ADDRESS_MODE, &config->madctl,
			      sizeof(config->madctl));
	if (ret < 0) {
		return ret;
	}

	ret = nv3007_send_cmd(dev, config->color_invert ? MIPI_DCS_ENTER_INVERT_MODE
							: MIPI_DCS_EXIT_INVERT_MODE,
			      NULL, 0);
	if (ret < 0) {
		return ret;
	}

	/* Tearing effect output on vertical blanking only, when the bus waits for it */
	if ((config->te_mode != MIPI_DBI_TE_NO_EDGE) &&
	    (mipi_dbi_configure_te(config->mipi_dbi, config->te_mode, config->te_delay) == 0)) {
		param = 0x00U;
		ret = nv3007_send_cmd(dev, MIPI_DCS_SET_TEAR_ON, &param, sizeof(param));
		if (ret < 0) {
			return ret;
		}
	}

	return nv3007_send_cmd(dev, MIPI_DCS_ENTER_NORMAL_MODE, NULL, 0);
}

static int nv3007_pm_action(const struct device *dev, enum pm_device_action action)
{
	const struct nv3007_config *config = dev->config;
	int ret;

	switch (action) {
	case PM_DEVICE_ACTION_TURN_ON:
		ret = mipi_dbi_reset(config->mipi_dbi, NV3007_RESET_PULSE_MS);
		if (ret < 0) {
			return ret;
		}
		nv3007_sleep_ms(NV3007_RESET_WAIT_MS);

		ret = nv3007_configure(dev);
		if (ret < 0) {
			LOG_ERR("Failed to configure the controller: %d", ret);
		}
		return ret;
	case PM_DEVICE_ACTION_SUSPEND:
		ret = nv3007_send_cmd(dev, MIPI_DCS_SET_DISPLAY_OFF, NULL, 0);
		if (ret < 0) {
			return ret;
		}
		ret = nv3007_send_cmd(dev, MIPI_DCS_ENTER_SLEEP_MODE, NULL, 0);
		if (ret < 0) {
			return ret;
		}
		nv3007_sleep_ms(NV3007_SLPIN_WAIT_MS);
		return 0;
	case PM_DEVICE_ACTION_RESUME:
		/* The display stays blanked until blanking_off() */
		ret = nv3007_send_cmd(dev, MIPI_DCS_EXIT_SLEEP_MODE, NULL, 0);
		if (ret < 0) {
			return ret;
		}
		nv3007_sleep_ms(NV3007_SLPOUT_WAIT_MS);
		return 0;
	default:
		return -ENOTSUP;
	}
}

static int nv3007_init(const struct device *dev)
{
	const struct nv3007_config *config = dev->config;

	if (!device_is_ready(config->mipi_dbi)) {
		LOG_ERR_DEVICE_NOT_READY(config->mipi_dbi);
		return -ENODEV;
	}

	return pm_device_driver_init(dev, nv3007_pm_action);
}

static DEVICE_API(display, nv3007_api) = {
	.blanking_on = nv3007_blanking_on,
	.blanking_off = nv3007_blanking_off,
	.write = nv3007_write,
	.get_capabilities = nv3007_get_capabilities,
	.set_pixel_format = nv3007_set_pixel_format,
	.set_orientation = nv3007_set_orientation,
};

#define NV3007_INIT(n)                                                                             \
	static const struct nv3007_config nv3007_config_##n = {                                    \
		.mipi_dbi = DEVICE_DT_GET(DT_INST_PARENT(n)),                                      \
		.dbi_config = {                                                                    \
			.config = MIPI_DBI_SPI_CONFIG_DT(DT_DRV_INST(n),                           \
							 SPI_OP_MODE_CONTROLLER | SPI_WORD_SET(8), \
							 0),                                       \
			.mode = DT_INST_STRING_UPPER_TOKEN_OR(n, mipi_mode,                        \
							      MIPI_DBI_MODE_SPI_4WIRE),            \
		},                                                                                 \
		.width = DT_INST_PROP(n, width),                                                   \
		.height = DT_INST_PROP(n, height),                                                 \
		.x_offset = DT_INST_PROP(n, x_offset),                                             \
		.y_offset = DT_INST_PROP(n, y_offset),                                             \
		.madctl = DT_INST_PROP(n, madctl),                                                 \
		.color_invert = DT_INST_PROP(n, color_invert),                                     \
		.te_mode = MIPI_DBI_TE_MODE_DT_INST(n, te_mode),                                   \
		.te_delay = DT_INST_PROP(n, te_delay),                                             \
	};                                                                                         \
                                                                                                   \
	PM_DEVICE_DT_INST_DEFINE(n, nv3007_pm_action);                                             \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(n, nv3007_init, PM_DEVICE_DT_INST_GET(n), NULL, &nv3007_config_##n,  \
			      POST_KERNEL, CONFIG_DISPLAY_INIT_PRIORITY, &nv3007_api);

DT_INST_FOREACH_STATUS_OKAY(NV3007_INIT)
