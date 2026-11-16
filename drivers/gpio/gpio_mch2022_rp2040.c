/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT badgeteam_mch2022_rp2040_gpio

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_utils.h>
#include <zephyr/drivers/mfd/mch2022_rp2040.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(gpio_mch2022_rp2040, CONFIG_GPIO_LOG_LEVEL);

#define PIN_CRESET   0U
#define PIN_NOT_DONE 1U

/* In the FPGA control register */
#define FPGA_CRESET_RELEASE BIT(0)

struct gpio_mch2022_rp2040_config {
	/* gpio_driver_config needs to be first */
	struct gpio_driver_config common;
	const struct device *mfd;
};

struct gpio_mch2022_rp2040_data {
	/* gpio_driver_data needs to be first */
	struct gpio_driver_data common;
};

static int gpio_mch2022_rp2040_set_creset(const struct device *dev, bool release)
{
	const struct gpio_mch2022_rp2040_config *config = dev->config;
	uint8_t reg;
	int ret;

	ret = mfd_mch2022_rp2040_read(config->mfd, MCH2022_RP2040_FPGA, &reg, 1U);
	if (ret < 0) {
		return ret;
	}

	WRITE_BIT(reg, 0, release);

	return mfd_mch2022_rp2040_write(config->mfd, MCH2022_RP2040_FPGA, &reg, 1U);
}

static int gpio_mch2022_rp2040_pin_configure(const struct device *dev, gpio_pin_t pin,
					     gpio_flags_t flags)
{
	if (pin == PIN_NOT_DONE) {
		return (flags & GPIO_OUTPUT) == 0U && (flags & GPIO_INPUT) != 0U ? 0 : -ENOTSUP;
	}

	if (pin != PIN_CRESET || (flags & GPIO_OUTPUT) == 0U) {
		return -ENOTSUP;
	}

	if ((flags & GPIO_OUTPUT_INIT_HIGH) != 0U) {
		return gpio_mch2022_rp2040_set_creset(dev, true);
	}

	if ((flags & GPIO_OUTPUT_INIT_LOW) != 0U) {
		return gpio_mch2022_rp2040_set_creset(dev, false);
	}

	return 0;
}

static int gpio_mch2022_rp2040_port_get_raw(const struct device *dev, gpio_port_value_t *value)
{
	const struct gpio_mch2022_rp2040_config *config = dev->config;
	uint8_t fpga;
	uint8_t input;
	int ret;

	ret = mfd_mch2022_rp2040_read(config->mfd, MCH2022_RP2040_FPGA, &fpga, 1U);
	if (ret < 0) {
		return ret;
	}

	ret = mfd_mch2022_rp2040_read(config->mfd, MCH2022_RP2040_INPUT1, &input, 1U);
	if (ret < 0) {
		return ret;
	}

	*value = ((fpga & FPGA_CRESET_RELEASE) != 0U ? BIT(PIN_CRESET) : 0U) |
		 ((input & BIT(MCH2022_RP2040_FPGA_NOT_DONE)) != 0U ? BIT(PIN_NOT_DONE) : 0U);

	return 0;
}

static int gpio_mch2022_rp2040_port_set_masked_raw(const struct device *dev,
						   gpio_port_pins_t mask,
						   gpio_port_value_t value)
{
	if ((mask & BIT(PIN_CRESET)) == 0U) {
		return 0;
	}

	return gpio_mch2022_rp2040_set_creset(dev, (value & BIT(PIN_CRESET)) != 0U);
}

static int gpio_mch2022_rp2040_port_set_bits_raw(const struct device *dev, gpio_port_pins_t pins)
{
	return gpio_mch2022_rp2040_port_set_masked_raw(dev, pins, pins);
}

static int gpio_mch2022_rp2040_port_clear_bits_raw(const struct device *dev,
						   gpio_port_pins_t pins)
{
	return gpio_mch2022_rp2040_port_set_masked_raw(dev, pins, 0U);
}

static int gpio_mch2022_rp2040_port_toggle_bits(const struct device *dev, gpio_port_pins_t pins)
{
	gpio_port_value_t value;
	int ret;

	ret = gpio_mch2022_rp2040_port_get_raw(dev, &value);
	if (ret < 0) {
		return ret;
	}

	return gpio_mch2022_rp2040_port_set_masked_raw(dev, pins, ~value);
}

static DEVICE_API(gpio, gpio_mch2022_rp2040_api) = {
	.pin_configure = gpio_mch2022_rp2040_pin_configure,
	.port_get_raw = gpio_mch2022_rp2040_port_get_raw,
	.port_set_masked_raw = gpio_mch2022_rp2040_port_set_masked_raw,
	.port_set_bits_raw = gpio_mch2022_rp2040_port_set_bits_raw,
	.port_clear_bits_raw = gpio_mch2022_rp2040_port_clear_bits_raw,
	.port_toggle_bits = gpio_mch2022_rp2040_port_toggle_bits,
};

static int gpio_mch2022_rp2040_init(const struct device *dev)
{
	const struct gpio_mch2022_rp2040_config *config = dev->config;

	if (!device_is_ready(config->mfd)) {
		LOG_ERR_DEVICE_NOT_READY(config->mfd);
		return -ENODEV;
	}

	return 0;
}

#define GPIO_MCH2022_RP2040_DEFINE(n)                                                              \
	static const struct gpio_mch2022_rp2040_config gpio_mch2022_rp2040_config_##n = {          \
		.common = {.port_pin_mask = GPIO_PORT_PIN_MASK_FROM_DT_INST(n)},                   \
		.mfd = DEVICE_DT_GET(DT_INST_PARENT(n)),                                           \
	};                                                                                         \
                                                                                                   \
	static struct gpio_mch2022_rp2040_data gpio_mch2022_rp2040_data_##n;                       \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(n, gpio_mch2022_rp2040_init, NULL, &gpio_mch2022_rp2040_data_##n,    \
			      &gpio_mch2022_rp2040_config_##n, POST_KERNEL,                        \
			      CONFIG_GPIO_MCH2022_RP2040_INIT_PRIORITY,                            \
			      &gpio_mch2022_rp2040_api);

DT_INST_FOREACH_STATUS_OKAY(GPIO_MCH2022_RP2040_DEFINE)
