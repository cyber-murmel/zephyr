/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @ingroup esp32_lcd_rgb_display_interface
 * @brief ESP32 LCD-CAM RGB panel: its vertical blanking
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_DISPLAY_ESP32_LCD_RGB_H_
#define ZEPHYR_INCLUDE_DRIVERS_DISPLAY_ESP32_LCD_RGB_H_

/**
 * @defgroup esp32_lcd_rgb_display_interface ESP32 LCD-CAM RGB panel
 * @brief ESP32 LCD-CAM RGB panel device-specific API extension
 * @since 4.5.0
 * @version 0.1.0
 * @ingroup display_interface
 * @{
 */

#include <zephyr/device.h>
#include <zephyr/kernel.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Wait for the panel's vertical blanking.
 *
 * Returns as a frame starts: then come the vertical sync pulse and the back porch, with no pixel
 * data and, once the controller's FIFO is full, no reads by the panel's DMA, only the pixel clock
 * and the syncs. A radio on the same board can take a burst in there that the panel's data and
 * its memory traffic stay out of. A frame start served late, with the frame under way, does not
 * count. One caller at a time.
 *
 * @param dev Panel device.
 * @param timeout How long to wait for a frame's start.
 * @param[out] blank_us On success, how long the blanking lasts from then, in microseconds: the
 *                      vertical sync pulse and the back porch. May be NULL.
 *
 * @retval 0 A frame has started.
 * @retval -EAGAIN No frame started within @p timeout.
 */
int display_esp32_lcd_rgb_wait_blanking(const struct device *dev, k_timeout_t timeout,
					 uint32_t *blank_us);

#ifdef __cplusplus
}
#endif

/**
 * @}
 */

#endif /* ZEPHYR_INCLUDE_DRIVERS_DISPLAY_ESP32_LCD_RGB_H_ */
