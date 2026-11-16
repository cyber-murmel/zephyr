.. zephyr:board:: mch2022

Overview
********

The `MCH2022 badge`_ is the badge of the May Contain Hackers 2022 camp, made by Badge.team: an
ESP32 with a landscape display, a joystick and buttons, an iCE40 FPGA and an RP2040 that bridges
USB to the ESP32 and reads the buttons.

Hardware
********

- ESP32-WROVER-E module (ESP32-D0WD-V3)

  - Dual core Xtensa LX6 up to 240 MHz
  - 16 MB flash
  - 8 MB PSRAM
  - Wi-Fi 802.11 b/g/n
  - Bluetooth 4.2 BR/EDR and BLE

- RP2040 coprocessor on I2C (address 0x17): USB, buttons, LCD backlight, battery and USB
  voltages, FPGA reset and CDONE
- 2.4 inch 320x240 TFT display, ILITEK ILI9341 controller on SPI, or on an 8-bit bus from the
  FPGA
- Lattice iCE40UP5K FPGA with 8 MB PSRAM, PMOD connector
- HOME, MENU, START, SELECT, A and B buttons and a five-way joystick
- Five SK6805 RGB LEDs
- Bosch BME680 environmental sensor (I2C address 0x77) and BNO055 orientation sensor (0x28)
- microSD slot, 1-bit SD mode
- MS4344 I2S audio DAC with speaker and headphone output
- Simple Add-On (SAO) and Qwiic connectors, infrared LED
- USB-C on the RP2040, LiPo charger

Pin assignments:

========================  =========================================
Function                  ESP32 GPIO
========================  =========================================
UART0 TX, RX (to RP2040)  IO1, IO3
I2C SCL, SDA              IO21, IO22
RP2040 interrupt          IO34 (low: a button changed)
SPI SCK, MOSI, MISO       IO18, IO23, IO35 (LCD and FPGA)
LCD CS, D/C, reset        IO32, IO33, IO25
LCD mode                  IO26 (low: SPI from the ESP32)
FPGA CS, interrupt        IO27, IO39
BNO055 interrupt          IO36
SD CLK, CMD, D0           IO14, IO15, IO2
SD card and LED power     IO19 (high: on)
LED data                  IO5
I2S MCLK, BCLK, WS, DOUT  IO0, IO4, IO12, IO13
========================  =========================================

The ``badgeteam,mch2022-rp2040`` node has two children: the buttons, which report Zephyr input
codes and also drive LVGL through a ``zephyr,lvgl-keypad-input`` node, and the backlight, an
LED device whose brightness the RP2040 sets by PWM. The display runs in landscape and takes RGB565
pixels with the most significant byte first. The SPI bus runs in half duplex: through the GPIO
matrix, full duplex stops at 26.7 MHz. The LEDs are driven by SPI2 on IO5.

The SD card and LED supply and the LCD mode are held by GPIO hogs. The FPGA, the BNO055 and the
audio DAC are not described.

.. include:: ../../../espressif/common/soc-esp32-features.rst
   :start-after: espressif-soc-esp32-features

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

The console and flashing go through the first USB serial port of the RP2040. The RP2040 resets
the ESP32 into its download mode on esptool's DTR and RTS sequence unless its reset lock is set.
The badge's stock firmware sets the lock at every boot and asks on the display before flashing;
the coprocessor driver clears it (:kconfig:option:`CONFIG_MFD_MCH2022_RP2040_DOWNLOAD_RESET`).

.. include:: ../../../espressif/common/board-variants.rst
   :start-after: espressif-board-variants

References
**********

.. target-notes::

.. _`MCH2022 badge`: https://badge.team/docs/badges/mch2022/
