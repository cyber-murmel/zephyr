.. zephyr:board:: ues31s043h800v480c_u

Overview
********

The VIEWE PLCM-UES31S043H800V480C-U is a 4.3 inch 800x480 IPS display module with capacitive
touch, built around the ESP32-S31-WROOM-3-N16R16V module: the ESP32-S31, a dual-core RISC-V SoC
with 2.4 GHz Wi-Fi 6, Bluetooth 5.4 and IEEE 802.15.4, 16 MB of flash and 16 MB of octal PSRAM.

For more information, check the `VIEWE product repository`_.

Hardware
********

- ST7262 RGB panel, 800x480, on the LCD side of LCD-CAM (18 data lines, driven with RGB565)
- GT911 capacitive touch controller on I2C0
- USB Type-C to a CH340C USB-to-UART bridge: power, console, flashing (with automatic reset into
  download mode). Open the port with DTR and RTS both asserted, which is what Linux drives on open:
  either one asserted alone, even for a moment while they change, acts. DTR alone holds BOOT down,
  RTS alone holds the chip in reset.
- USB 2.0 Type-A on the OTG high-speed port. **Its VBUS is driven whenever the board has power**,
  through a TPS2051 whose enable is tied high: connecting it to a host with an A-to-A cable ties two
  5 V rails together, so use one with VBUS cut.
- ES8389 audio codec on I2C0 and I2S, an amplifier enabled by GPIO47
- microSD socket, power switched by GPIO60 (active low)
- WS2812 RGB LED, on GPIO37
- Four buttons on one ADC channel (GPIO42), plus BOOT on GPIO61
- CAN (SIT1050T, TX GPIO53, RX GPIO54) and RS485 (SIT3088E, TX GPIO35, RX GPIO36)
- Header H1 with UART1 (TX GPIO33, RX GPIO34: also the USB Serial/JTAG's D-, D+), GPIO53..57

.. include:: ../../../espressif/common/soc-esp32s31-features.rst
   :start-after: espressif-soc-esp32s31-features

Pin use
=======

.. list-table::
   :header-rows: 1

   * - Function
     - Pads
   * - Panel data
     - R2..R7 GPIO2..7, G2..G7 GPIO8..13, B2..B7 GPIO14..19
   * - Panel control
     - PCLK GPIO40, DE GPIO43, HSYNC GPIO44, VSYNC GPIO45
   * - Backlight enable
     - GPIO39
   * - I2C0
     - SDA GPIO0, SCL GPIO1
   * - Touch interrupt
     - GPIO38 (its reset is an RC to 3V3, not driven)
   * - I2S
     - MCLK GPIO48, BCK GPIO49, WS GPIO50, DOUT GPIO51, DIN GPIO52
   * - microSD
     - CLK GPIO24, CMD GPIO25, D0..D3 GPIO20..23

The panel takes six bits of each color and RGB565 drives five bits of red and blue: the sixth wires
of those two, R2 and B2, repeat R7 and B7, so that full scale is full white.

The LP core's UART is fixed on GPIO6 and GPIO7, the panel's R6 and R7, which reach no header: the
``lpcore`` target's console needs the panel off and a wire on the panel's connector.

Display
=======

The framebuffer is one 768 KB frame in PSRAM, scanned out at 25.1 MHz, about 58 frames a second,
which reads 50 MB/s from the PSRAM while a line goes out. The board therefore runs the PSRAM at
250 MHz, the SoC's highest: through the cache the CPU reads 90 MB/s from it, 82 MB/s at 200 MHz.
At the SoC's default of 40 MHz the panel works as long as the CPU leaves the PSRAM alone; with the
CPU copying within it, every frame underruns. The flash runs in QIO mode, which reads 28 MB/s
through the cache against 16 MB/s in DIO. The vertical back porch is 32 lines where
the vendor's timing has 8: with the 4 lines of the VSYNC pulse that is 1.18 ms from a frame's start
to its first line, room for a burst of the SoC's own receiver
(``display_esp32_lcd_rgb_wait_blanking()``). With ``CONFIG_DISPLAY_ESP32_LCD_RGB_DOUBLE_BUFFER`` a
second framebuffer takes another 768 KB, and the board's PSRAM heap grows from 2 to 3 MB.

The pixel clock's multiples reach the SoC's own receiver. From PLL_F160M at 25 MHz they were lines
9 to 12 dB over the noise floor in 250 kHz near 2.35 GHz. The board takes the clock from the APLL at
25.1 MHz instead, which leaves the APLL's setting a fraction: there the multiples read the floor,
and the floor between them moved by 0 to 3 dB. The panel's pads run at the weakest drive, 5 mA
nominal, which took the lines from PLL_F160M 5 to 7 dB down, the picture unchanged. Lines at the
PSRAM clock's multiples, 2250, 2500 and 2750 MHz at 250 MHz, do not follow the pixel clock. In a
burst taken in the vertical blanking, where the panel's DMA is idle, they come 12 to 27 dB down;
2400 MHz is the chip's own line, about -54 dBFS either way.

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

.. include:: ../../../espressif/common/board-variants.rst
   :start-after: espressif-board-variants

Debugging
=========

The USB Serial/JTAG has no connector on this board: its data lines are GPIO33 and GPIO34 on header
H1. With a cable wired to them, OpenOCD should work as on other ESP32-S31 boards; this has not been
tried:

.. include:: ../../../espressif/common/openocd-debugging.rst
   :start-after: espressif-openocd-debugging

References
**********

.. target-notes::

.. _`VIEWE product repository`: https://github.com/VIEWESMART/PLCM-UES31S043H800V480C-U
