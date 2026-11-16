.. zephyr:board:: communicator_badge

Overview
********

The Hackaday `Communicator Badge`_ is the badge of the 2025 Hackaday Supercon, also used at
Hackaday Europe 2026: a handheld ESP32-S3 device with a wide landscape display, a QWERTY keyboard
and a LoRa radio, programmed over its USB-C port.

Hardware
********

- ESP32-S3-WROOM-1-N16R8 module

  - Dual core Xtensa LX7 up to 240 MHz
  - 16 MB flash
  - 8 MB octal PSRAM
  - Wi-Fi 802.11 b/g/n
  - Bluetooth LE 5.0

- 2.79 inch 428x142 TFT display, New Vision NV3007 controller on SPI, backlight on IO2
- 69-key keyboard on a TI TCA8418 keypad scan controller (I2C address 0x34)
- Seeed Wio-SX1262 LoRa module (862 to 930 MHz) with an edge SMA connector behind a pi
  attenuator; bridge JP1 to bypass it
- Simple Add-On (SAO) header with I2C and two GPIOs
- Side LED on IO1
- USB-C on the native USB of the ESP32-S3, MCP73831 charger for a single cell LiPo battery,
  power switch

Pin assignments:

========================  =========================================
Function                  ESP32-S3 GPIO
========================  =========================================
Display SCK, MOSI         IO38, IO21
Display CS, D/C, reset    IO41, IO39, IO40
Display TE                IO42
Display backlight         IO2 (high: on)
Keyboard SCL, SDA         IO14, IO47
Keyboard INT, reset       IO13, IO48
LoRa SCK, MOSI, MISO      IO8, IO3, IO9
LoRa NSS, reset           IO17, IO18
LoRa BUSY, DIO1           IO15, IO16
LoRa RX switch            IO10 (high: receive)
SAO SCL, SDA              IO5, IO4
SAO GPIO1, GPIO2          IO7, IO6
Side LED                  IO1 (low: on)
UART0 TX, RX (J7)         IO43, IO44
Expansion (J6)            IO11, IO12
========================  =========================================

The keyboard reports Zephyr input key codes through an ``input-keymap``; the arrows, Enter, Esc,
Backspace and Tab also drive LVGL through a ``zephyr,lvgl-keypad-input`` node. The display runs
in landscape (MADCTL 0xA0) and takes RGB565 pixels with the most significant byte first.

The backlight pin is held high by a GPIO hog. To dim it, remove the hog in an overlay and route
IO2 to ``ledc0``.

.. include:: ../../../espressif/common/soc-esp32s3-features.rst
   :start-after: espressif-soc-esp32s3-features

Supported Features
==================

.. zephyr:board-supported-hw::

System Requirements
*******************

.. include:: ../../../espressif/common/system-requirements.rst
   :start-after: espressif-system-requirements

Programming and Debugging
*************************

.. zephyr:board-supported-runners::

.. include:: ../../../espressif/common/building-flashing.rst
   :start-after: espressif-building-flashing

The console and flashing use the USB-Serial-JTAG of the ESP32-S3. The board has a reset button
but no boot button: to enter the boot ROM download mode by hand, short test pad TP1 (IO0) to
ground while pressing reset.

.. include:: ../../../espressif/common/board-variants.rst
   :start-after: espressif-board-variants

Debugging
=========

.. include:: ../../../espressif/common/openocd-debugging.rst
   :start-after: espressif-openocd-debugging

References
**********

.. target-notes::

.. _`Communicator Badge`: https://github.com/Hack-a-Day/2025-Communicator_Badge
