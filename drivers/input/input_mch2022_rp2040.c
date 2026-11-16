/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT badgeteam_mch2022_rp2040_input

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/mfd/mch2022_rp2040.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(input_mch2022_rp2040, CONFIG_INPUT_LOG_LEVEL);

/* Button bits in the order of the zephyr,codes property */
static const uint8_t mch2022_rp2040_buttons[] = {
	MCH2022_RP2040_BTN_HOME,
	MCH2022_RP2040_BTN_MENU,
	MCH2022_RP2040_BTN_START,
	MCH2022_RP2040_BTN_ACCEPT,
	MCH2022_RP2040_BTN_BACK,
	MCH2022_RP2040_BTN_SELECT,
	MCH2022_RP2040_BTN_JOY_LEFT,
	MCH2022_RP2040_BTN_JOY_PRESS,
	MCH2022_RP2040_BTN_JOY_DOWN,
	MCH2022_RP2040_BTN_JOY_UP,
	MCH2022_RP2040_BTN_JOY_RIGHT,
};

#define MCH2022_RP2040_NUM_BUTTONS ARRAY_SIZE(mch2022_rp2040_buttons)

/* The RP2040 releases the interrupt line once the bus is idle again */
#define MCH2022_RP2040_INT_RECHECK_MS 10U

struct mch2022_rp2040_input_config {
	const struct device *mfd;
	struct gpio_dt_spec int_gpio;
	uint32_t poll_period_ms;
	uint16_t codes[MCH2022_RP2040_NUM_BUTTONS];
};

struct mch2022_rp2040_input_data {
	const struct device *dev;
	struct k_work_delayable work;
	struct gpio_callback int_gpio_cb;
	uint16_t state;
};

static int mch2022_rp2040_input_read(const struct device *dev)
{
	const struct mch2022_rp2040_input_config *config = dev->config;
	struct mch2022_rp2040_input_data *data = dev->data;
	uint8_t buf[4];
	uint16_t state;
	uint16_t changed;
	uint16_t report = 0U;
	int ret;

	/* INPUT1, INPUT2, INTERRUPT1, INTERRUPT2: reading the last one clears the interrupt */
	ret = mfd_mch2022_rp2040_read(config->mfd, MCH2022_RP2040_INPUT1, buf, sizeof(buf));
	if (ret < 0) {
		return ret;
	}

	state = sys_get_le16(&buf[0]);
	changed = sys_get_le16(&buf[2]) | (state ^ data->state);
	data->state = state;

	for (size_t i = 0; i < MCH2022_RP2040_NUM_BUTTONS; i++) {
		bool button_changed = (changed & BIT(mch2022_rp2040_buttons[i])) != 0U;

		if (button_changed && (config->codes[i] != 0U)) {
			report |= BIT(i);
		}
	}

	while (report != 0U) {
		size_t i = find_lsb_set(report) - 1U;

		report &= ~BIT(i);
		input_report_key(dev, config->codes[i],
				 (state & BIT(mch2022_rp2040_buttons[i])) != 0U, report == 0U,
				 K_FOREVER);
	}

	return 0;
}

static void mch2022_rp2040_input_work_handler(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct mch2022_rp2040_input_data *data =
		CONTAINER_OF(dwork, struct mch2022_rp2040_input_data, work);
	const struct device *dev = data->dev;
	const struct mch2022_rp2040_input_config *config = dev->config;
	int ret;

	ret = mch2022_rp2040_input_read(dev);
	if (ret < 0) {
		LOG_ERR("Failed to read the buttons: %d", ret);
	}

	if (config->int_gpio.port == NULL) {
		k_work_reschedule(dwork, K_MSEC(config->poll_period_ms));
		return;
	}

	/* An edge interrupt never comes again while the line stays asserted */
	if ((ret < 0) || (gpio_pin_get_dt(&config->int_gpio) == 1)) {
		k_work_reschedule(dwork, K_MSEC(MCH2022_RP2040_INT_RECHECK_MS));
	}
}

static void mch2022_rp2040_input_isr(const struct device *port, struct gpio_callback *cb,
				     uint32_t pins)
{
	struct mch2022_rp2040_input_data *data =
		CONTAINER_OF(cb, struct mch2022_rp2040_input_data, int_gpio_cb);

	ARG_UNUSED(port);
	ARG_UNUSED(pins);

	k_work_reschedule(&data->work, K_NO_WAIT);
}

static int mch2022_rp2040_input_init_interrupt(const struct device *dev)
{
	const struct mch2022_rp2040_input_config *config = dev->config;
	struct mch2022_rp2040_input_data *data = dev->data;
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

	gpio_init_callback(&data->int_gpio_cb, mch2022_rp2040_input_isr,
			   BIT(config->int_gpio.pin));
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

static int mch2022_rp2040_input_init(const struct device *dev)
{
	const struct mch2022_rp2040_input_config *config = dev->config;
	struct mch2022_rp2040_input_data *data = dev->data;
	int ret;

	if (!device_is_ready(config->mfd)) {
		LOG_ERR_DEVICE_NOT_READY(config->mfd);
		return -ENODEV;
	}

	data->dev = dev;
	k_work_init_delayable(&data->work, mch2022_rp2040_input_work_handler);

	ret = mch2022_rp2040_input_init_interrupt(dev);
	if (ret < 0) {
		return ret;
	}

	/* Takes the state, clears a pending interrupt, or starts polling */
	k_work_schedule(&data->work, K_NO_WAIT);

	return 0;
}

#define MCH2022_RP2040_INPUT_INIT(n)                                                               \
	BUILD_ASSERT(DT_INST_PROP_LEN(n, zephyr_codes) == MCH2022_RP2040_NUM_BUTTONS,              \
		     "zephyr,codes needs one code per button");                                    \
                                                                                                   \
	static const struct mch2022_rp2040_input_config mch2022_rp2040_input_config_##n = {        \
		.mfd = DEVICE_DT_GET(DT_INST_PARENT(n)),                                           \
		.int_gpio = GPIO_DT_SPEC_INST_GET_OR(n, int_gpios, {0}),                           \
		.poll_period_ms = DT_INST_PROP(n, poll_period_ms),                                 \
		.codes = DT_INST_PROP(n, zephyr_codes),                                            \
	};                                                                                         \
                                                                                                   \
	static struct mch2022_rp2040_input_data mch2022_rp2040_input_data_##n;                     \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(n, mch2022_rp2040_input_init, NULL, &mch2022_rp2040_input_data_##n,  \
			      &mch2022_rp2040_input_config_##n, POST_KERNEL,                       \
			      CONFIG_INPUT_INIT_PRIORITY, NULL);

DT_INST_FOREACH_STATUS_OKAY(MCH2022_RP2040_INPUT_INIT)
