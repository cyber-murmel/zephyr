/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief MCH2022 badge RP2040 coprocessor
 *
 * The badge's RP2040 bridges USB to the ESP32 and reads the buttons, the
 * battery and USB voltages and the FPGA's CDONE line. It answers on I2C as a
 * bank of byte registers with an auto-incrementing address; writes take
 * effect once the bus is idle again.
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_MFD_MCH2022_RP2040_H_
#define ZEPHYR_INCLUDE_DRIVERS_MFD_MCH2022_RP2040_H_

#include <stddef.h>
#include <stdint.h>

#include <zephyr/device.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @name Registers
 * @{
 */
/** Firmware version; 0xFF while the RP2040 runs its own bootloader */
#define MCH2022_RP2040_FW_VER          0x00U
/** SAO and prototyping area GPIO direction, 1 = output */
#define MCH2022_RP2040_GPIO_DIR        0x01U
/** SAO and prototyping area GPIO output level */
#define MCH2022_RP2040_GPIO_OUT        0x03U
/** LCD backlight PWM duty, 0 to 255 */
#define MCH2022_RP2040_LCD_BACKLIGHT   0x04U
/** FPGA control: bit 0 releases CRESET_B */
#define MCH2022_RP2040_FPGA            0x05U
/** Button state, 1 = pressed; INPUT2 follows */
#define MCH2022_RP2040_INPUT1          0x06U
/** Buttons changed since the last read of INTERRUPT2; INTERRUPT2 follows */
#define MCH2022_RP2040_INTERRUPT1      0x08U
/** Reading this register clears both interrupt registers and the IRQ line */
#define MCH2022_RP2040_INTERRUPT2      0x09U
/** USB supply voltage, 12-bit ADC code, little endian */
#define MCH2022_RP2040_ADC_VUSB        0x0BU
/** Battery voltage, 12-bit ADC code, little endian */
#define MCH2022_RP2040_ADC_VBAT        0x0DU
/** Nonzero blocks the download mode reset requested over USB */
#define MCH2022_RP2040_RESET_LOCK      0x13U
/** Set when a download mode reset was blocked */
#define MCH2022_RP2040_RESET_ATTEMPTED 0x14U
/** 1 while the battery charges */
#define MCH2022_RP2040_CHARGING_STATE  0x15U
/** @} */

/**
 * @name Button bits in INPUT1 | INPUT2 << 8 and INTERRUPT1 | INTERRUPT2 << 8
 * @{
 */
#define MCH2022_RP2040_BTN_HOME       0U
#define MCH2022_RP2040_BTN_MENU       1U
#define MCH2022_RP2040_BTN_START      2U
#define MCH2022_RP2040_BTN_ACCEPT     3U
#define MCH2022_RP2040_BTN_BACK       4U
/** Not a button: set while the FPGA is not configured */
#define MCH2022_RP2040_FPGA_NOT_DONE  5U
#define MCH2022_RP2040_BTN_SELECT     7U
#define MCH2022_RP2040_BTN_JOY_LEFT   8U
#define MCH2022_RP2040_BTN_JOY_PRESS  9U
#define MCH2022_RP2040_BTN_JOY_DOWN   10U
#define MCH2022_RP2040_BTN_JOY_UP     11U
#define MCH2022_RP2040_BTN_JOY_RIGHT  12U
/** @} */

/** Firmware version that added RESET_LOCK and RESET_ATTEMPTED */
#define MCH2022_RP2040_FW_VER_RESET_LOCK 0x09U

/**
 * @brief Read consecutive coprocessor registers.
 *
 * @param dev MFD device.
 * @param reg First register.
 * @param buf Destination.
 * @param len Number of registers.
 *
 * @retval 0 on success.
 * @retval -errno I2C error.
 */
int mfd_mch2022_rp2040_read(const struct device *dev, uint8_t reg, uint8_t *buf, size_t len);

/**
 * @brief Write consecutive coprocessor registers.
 *
 * @param dev MFD device.
 * @param reg First register.
 * @param buf Source.
 * @param len Number of registers.
 *
 * @retval 0 on success.
 * @retval -errno I2C error.
 */
int mfd_mch2022_rp2040_write(const struct device *dev, uint8_t reg, const uint8_t *buf,
			     size_t len);

/**
 * @brief Coprocessor firmware version read at init.
 *
 * @param dev MFD device.
 *
 * @return Firmware version.
 */
uint8_t mfd_mch2022_rp2040_fw_version(const struct device *dev);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_MFD_MCH2022_RP2040_H_ */
