/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT badgeteam_mch2022_rp2040_backlight

#include <zephyr/device.h>
#include <zephyr/drivers/led.h>
#include <zephyr/drivers/mfd/mch2022_rp2040.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(led_mch2022_rp2040, CONFIG_LED_LOG_LEVEL);

#define MCH2022_RP2040_BACKLIGHT_MAX 255U

struct led_mch2022_rp2040_config {
	const struct device *mfd;
};

static int led_mch2022_rp2040_set_brightness(const struct device *dev, uint32_t led,
					     uint8_t value)
{
	const struct led_mch2022_rp2040_config *config = dev->config;
	uint8_t duty;

	if (led != 0U) {
		return -EINVAL;
	}

	duty = (uint8_t)DIV_ROUND_CLOSEST((uint32_t)value * MCH2022_RP2040_BACKLIGHT_MAX,
					  LED_BRIGHTNESS_MAX);

	return mfd_mch2022_rp2040_write(config->mfd, MCH2022_RP2040_LCD_BACKLIGHT, &duty, 1U);
}

static DEVICE_API(led, led_mch2022_rp2040_api) = {
	.set_brightness = led_mch2022_rp2040_set_brightness,
};

static int led_mch2022_rp2040_init(const struct device *dev)
{
	const struct led_mch2022_rp2040_config *config = dev->config;

	if (!device_is_ready(config->mfd)) {
		LOG_ERR_DEVICE_NOT_READY(config->mfd);
		return -ENODEV;
	}

	return 0;
}

#define LED_MCH2022_RP2040_DEFINE(n)                                                               \
	static const struct led_mch2022_rp2040_config led_mch2022_rp2040_config_##n = {            \
		.mfd = DEVICE_DT_GET(DT_INST_PARENT(n)),                                           \
	};                                                                                         \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(n, led_mch2022_rp2040_init, NULL, NULL,                              \
			      &led_mch2022_rp2040_config_##n, POST_KERNEL,                         \
			      CONFIG_LED_INIT_PRIORITY, &led_mch2022_rp2040_api);

DT_INST_FOREACH_STATUS_OKAY(LED_MCH2022_RP2040_DEFINE)
