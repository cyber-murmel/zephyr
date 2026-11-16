.. zephyr:board:: why2025_badge

Overview
********

The `WHY2025 badge`_ is the badge of the WHY2025 hacker camp: an ESP32-P4 compute module in an M.2
slot of a carrier board with a 720x720 display, a full keyboard and an ESP32-C6 radio
co-processor. Both SoCs are Zephyr targets of this board:

- ``why2025_badge/esp32p4/hpcore``: the application processor with the display, keyboard,
  microSD card and sensors. Wi-Fi and Bluetooth come from the ESP32-C6 running the
  esp-hosted-mcu slave firmware.
- ``why2025_badge/esp32c6/hpcore``: the ESP32-C6 itself, with its own Wi-Fi, Bluetooth LE and
  IEEE 802.15.4 radio, the display and keyboard backlights and the LoRa module. An image built
  for this target replaces the esp-hosted-mcu slave firmware.

Hardware
********

- ESP32-P4NRW16 compute module

  - Dual core RISC-V at 360 MHz, pre-v3 silicon (v1.0 seen; it boots with a warning that it is
    below the v1.3 the board configures)
  - 16 MB 1.8 V flash, 32 MB PSRAM
  - CH340K USB-UART bridge on UART0 (IO37, IO38) with auto reset

- 3.95 inch 720x720 IPS display, Sitronix ST7703 on 2 MIPI DSI lanes
- Full keyboard on a TI TCA8418 keypad scan controller (I2C address 0x34)
- ESP32-C6-WROOM-1-N8 radio co-processor on SDIO, Ai-Thinker Ra-01SH (SX1262) LoRa module
  on the ESP32-C6 (the schematic names the Ra-01H, SX1276; the SMA connector is not fitted)
- microSD card slot, MAX98357A speaker amplifier, BMI270 IMU, BME690 environmental sensor
- Two 18650 cells with an IP5306 charger, power button
- USB-C port J8 behind a multiplexer: the ESP32-P4 USB OTG controller or the ESP32-C6
  USB-Serial-JTAG, selected by ESP32-P4 GPIO2

ESP32-P4 pin assignments:

========================  =========================================
Function                  ESP32-P4 GPIO
========================  =========================================
Console (CH340K)          IO37 (TX), IO38 (RX)
I2C (keyboard, sensors)   IO18 (SDA), IO20 (SCL)
I2C on header J7          IO22 (SDA), IO9 (SCL)
Display reset             IO17 (high: reset, through a transistor)
microSD                   IO39 to IO42 (D0 to D3), IO43 (CLK), IO44 (CMD)
ESP32-C6 SDIO             IO28, IO31, IO30, IO29 (D0 to D3), IO32 (CLK), IO33 (CMD)
ESP32-C6 enable           IO12 (low: reset), also the keyboard controller reset
ESP32-C6 boot, interrupt  IO13, IO14
ESP32-C6 UART0            IO15 (RX), IO16 (TX)
USB-C J8 multiplexer      IO2 (high: ESP32-C6)
Speaker amplifier         IO25 (LRCLK), IO26 (BCLK), IO27 (DIN), IO24 (shutdown, low)
Blue LED                  IO51 (high: on)
========================  =========================================

ESP32-C6 pin assignments:

========================  =========================================
Function                  ESP32-C6 GPIO
========================  =========================================
Display backlight         IO15 (high: on)
Keyboard backlight        IO10 (high: on)
LoRa SCK, MOSI, MISO      IO6, IO7, IO2
LoRa NSS, reset, TXEN     IO4, IO1, IO3
ESP32-P4 UART1            IO16 (TX), IO17 (RX)
========================  =========================================

The keyboard interrupt line only reaches header J7, so the ESP32-P4 polls the TCA8418. The
keyboard reports Zephyr input key codes through an ``input-keymap``; the six symbol keys on the
top row map to ``INPUT_KEY_F1`` to ``INPUT_KEY_F6`` and the Fn key to ``INPUT_KEY_RIGHTMETA``. The
arrows, Enter, Esc, Backspace and Tab also drive LVGL.

The devicetree describes the panel with the black bezel, used on the production boards. The
backlights are driven by the ESP32-C6, so the display stays dark unless the ESP32-C6 firmware
turns them on; the ``why2025_badge/esp32c6/hpcore`` target holds both backlight pins high. The
BME690 and the speaker amplifier are not described.

.. include:: ../../../espressif/common/soc-esp32p4-features.rst
   :start-after: espressif-soc-esp32p4-features

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

The ESP32-P4 is flashed through the CH340K, whose DTR and RTS lines drive its boot and reset pins.

To flash the ESP32-C6, first run an ESP32-P4 image built with
:kconfig:option:`CONFIG_WHY2025_BADGE_USB_TO_C6` and without the ``esp-hosted-mcu`` node, whose
driver resets the ESP32-C6 and expects the slave firmware on it. USB-C port J8 then reaches the
USB-Serial-JTAG of the ESP32-C6, which flashes like any ESP32-C6 board.

.. include:: ../../../espressif/common/board-variants.rst
   :start-after: espressif-board-variants

Debugging
=========

.. include:: ../../../espressif/common/openocd-debugging.rst
   :start-after: espressif-openocd-debugging

References
**********

.. target-notes::

.. _`WHY2025 badge`: https://gitlab.com/why2025/team-badge
