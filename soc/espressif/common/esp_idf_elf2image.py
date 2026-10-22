#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0
"""esptool for application images that the ESP-IDF 2nd stage bootloader starts.

The linker script leaves alignment gaps between the read-only data sections.
esptool pads such gaps only for images with a RAM-only header and otherwise
keeps each section as its own segment, while the ESP-IDF bootloader takes
exactly one read-only data segment. Pad the gaps the same way here, then run
esptool with the given arguments.
"""

import sys

from esptool import bin_image, main

_merge_adjacent_segments = bin_image.BaseFirmwareImage.merge_adjacent_segments


def _merge_adjacent_segments_padded(self):
    ram_only_header = self.ram_only_header
    self.ram_only_header = True
    try:
        _merge_adjacent_segments(self)
    finally:
        self.ram_only_header = ram_only_header


bin_image.BaseFirmwareImage.merge_adjacent_segments = _merge_adjacent_segments_padded

if __name__ == "__main__":
    sys.exit(main())
