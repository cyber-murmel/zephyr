/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <esp_app_desc.h>
#include <zephyr/sys/util.h>
#include <zephyr/toolchain.h>
#include <zephyr/version.h>

/*
 * The ESP-IDF bootloader and esp_ota_end() expect this descriptor at the
 * start of the first flash segment: the linker script places the section
 * at the start of the read-only data, and esptool moves it to the front of
 * the image and fills in app_elf_sha256.
 */
const __used __section(".flash.appdesc") esp_app_desc_t esp_app_desc = {
	.magic_word = ESP_APP_DESC_MAGIC_WORD,
	.version = KERNEL_VERSION_STRING,
	.project_name = "zephyr",
	.idf_ver = "zephyr",
	.time = __TIME__,
	.date = __DATE__,
	.mmu_page_size = LOG2CEIL(CONFIG_MMU_PAGE_SIZE),
};

BUILD_ASSERT(sizeof(esp_app_desc_t) == 256);
