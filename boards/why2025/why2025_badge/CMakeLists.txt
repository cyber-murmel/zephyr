# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

if(CONFIG_WHY2025_BADGE_USB_TO_C6)
  zephyr_library()
  zephyr_library_sources(board.c)
endif()
