/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/init.h>

/* GPIO2 selects USB-C port J8 for the ESP32-C6 (high) or the ESP32-P4 (low) */
#define USB_MUX_PIN 2

void board_late_init_hook(void)
{
	const struct device *gpio = DEVICE_DT_GET(DT_NODELABEL(gpio0));

	if (device_is_ready(gpio)) {
		(void)gpio_pin_configure(gpio, USB_MUX_PIN, GPIO_OUTPUT_HIGH);
	}
}
