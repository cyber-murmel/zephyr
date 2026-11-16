/*
 * Copyright (c) 2019 Manivannan Sadhasivam
 * Copyright (c) 2020 Andreas Sandberg
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

#include "sx126x_common.h"

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(sx126x, CONFIG_LORA_LOG_LEVEL);

static const struct gpio_dt_spec sx126x_gpio_reset = GPIO_DT_SPEC_INST_GET(
		0, reset_gpios);
#define HAVE_GPIO_BUSY DT_INST_NODE_HAS_PROP(0, busy_gpios)
#define HAVE_GPIO_DIO1 DT_INST_NODE_HAS_PROP(0, dio1_gpios)

/*
 * Without a BUSY line, each command is followed by its worst-case busy time:
 * calibration and the wakeup from sleep (cold start) take up to 3.5 ms, every
 * other command well under a millisecond.
 */
#define SX126X_BUSY_LONG_US  4000
#define SX126X_BUSY_SHORT_US 1000

/* Without a DIO1 line, the IRQ status is polled at this period. */
#define SX126X_DIO1_POLL_MS 5

#if HAVE_GPIO_BUSY
static const struct gpio_dt_spec sx126x_gpio_busy = GPIO_DT_SPEC_INST_GET(
		0, busy_gpios);
#endif
#if HAVE_GPIO_DIO1
static const struct gpio_dt_spec sx126x_gpio_dio1 = GPIO_DT_SPEC_INST_GET(
		0, dio1_gpios);
#endif

void sx126x_reset(struct sx126x_data *dev_data)
{
	gpio_pin_set_dt(&sx126x_gpio_reset, 1);
	k_sleep(K_MSEC(20));
	gpio_pin_set_dt(&sx126x_gpio_reset, 0);
	k_sleep(K_MSEC(10));
}

bool sx126x_is_busy(struct sx126x_data *dev_data)
{
#if HAVE_GPIO_BUSY
	return gpio_pin_get_dt(&sx126x_gpio_busy);
#else
	switch (dev_data->last_cmd) {
	case 0:
		break;
	case RADIO_CALIBRATE:
	case RADIO_CALIBRATEIMAGE:
	case RADIO_GET_STATUS:
		k_sleep(K_USEC(SX126X_BUSY_LONG_US));
		break;
	default:
		k_sleep(K_USEC(SX126X_BUSY_SHORT_US));
		break;
	}
	/* Waited once: the next wait before a command returns at once. */
	dev_data->last_cmd = 0;
	return false;
#endif
}

uint32_t sx126x_get_dio1_pin_state(struct sx126x_data *dev_data)
{
#if HAVE_GPIO_DIO1
	return gpio_pin_get_dt(&sx126x_gpio_dio1) > 0 ? 1U : 0U;
#else
	return SX126xGetIrqStatus() != 0U ? 1U : 0U;
#endif
}

void sx126x_dio1_irq_enable(struct sx126x_data *dev_data)
{
#if HAVE_GPIO_DIO1
	gpio_pin_interrupt_configure_dt(&sx126x_gpio_dio1,
				     GPIO_INT_EDGE_TO_ACTIVE);
#else
	k_work_schedule(&dev_data->dio1_poll_work, K_MSEC(SX126X_DIO1_POLL_MS));
#endif
}

void sx126x_dio1_irq_disable(struct sx126x_data *dev_data)
{
#if HAVE_GPIO_DIO1
	gpio_pin_interrupt_configure_dt(&sx126x_gpio_dio1,
				     GPIO_INT_DISABLE);
#else
	k_work_cancel_delayable(&dev_data->dio1_poll_work);
#endif
}

#if HAVE_GPIO_DIO1
static void sx126x_dio1_irq_callback(const struct device *dev,
				     struct gpio_callback *cb, uint32_t pins)
{
	struct sx126x_data *dev_data = CONTAINER_OF(cb, struct sx126x_data,
						    dio1_irq_callback);

	if (pins & BIT(sx126x_gpio_dio1.pin)) {
		k_work_submit(&dev_data->dio1_irq_work);
	}
}
#else
/* A pending IRQ goes to the handler, which clears it; the poll goes on meanwhile. */
static void sx126x_dio1_poll_work_handler(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct sx126x_data *dev_data = CONTAINER_OF(dwork, struct sx126x_data,
						    dio1_poll_work);

	if (SX126xGetIrqStatus() != 0U) {
		k_work_submit(&dev_data->dio1_irq_work);
	}
	k_work_schedule(dwork, K_MSEC(SX126X_DIO1_POLL_MS));
}
#endif

void sx126x_set_tx_params(int8_t power, RadioRampTimes_t ramp_time)
{
	SX126xSetTxParams(power, ramp_time);
}

int sx126x_variant_init(const struct device *dev)
{
	struct sx126x_data *dev_data = dev->data;

	if (gpio_pin_configure_dt(&sx126x_gpio_reset, GPIO_OUTPUT_ACTIVE) != 0) {
		LOG_ERR("GPIO configuration failed.");
		return -EIO;
	}

#if HAVE_GPIO_BUSY
	if (gpio_pin_configure_dt(&sx126x_gpio_busy, GPIO_INPUT) != 0) {
		LOG_ERR("GPIO configuration failed.");
		return -EIO;
	}
#endif

#if HAVE_GPIO_DIO1
	if (gpio_pin_configure_dt(&sx126x_gpio_dio1, GPIO_INPUT) != 0) {
		LOG_ERR("GPIO configuration failed.");
		return -EIO;
	}

	gpio_init_callback(&dev_data->dio1_irq_callback,
			   sx126x_dio1_irq_callback, BIT(sx126x_gpio_dio1.pin));
	if (gpio_add_callback(sx126x_gpio_dio1.port,
			      &dev_data->dio1_irq_callback) < 0) {
		LOG_ERR("Could not set GPIO callback for DIO1 interrupt.");
		return -EIO;
	}
#else
	k_work_init_delayable(&dev_data->dio1_poll_work, sx126x_dio1_poll_work_handler);
#endif

	return 0;
}
