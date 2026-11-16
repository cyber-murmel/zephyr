/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT badgeteam_mch2022_rp2040

#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/mfd/mch2022_rp2040.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(mfd_mch2022_rp2040, CONFIG_MFD_LOG_LEVEL);

/* The register bank's version while the RP2040 runs its bootloader */
#define MCH2022_RP2040_FW_VER_BOOTLOADER 0xFFU

struct mfd_mch2022_rp2040_config {
	struct i2c_dt_spec i2c;
};

struct mfd_mch2022_rp2040_data {
	uint8_t fw_ver;
};

int mfd_mch2022_rp2040_read(const struct device *dev, uint8_t reg, uint8_t *buf, size_t len)
{
	const struct mfd_mch2022_rp2040_config *config = dev->config;

	return i2c_burst_read_dt(&config->i2c, reg, buf, len);
}

int mfd_mch2022_rp2040_write(const struct device *dev, uint8_t reg, const uint8_t *buf,
			     size_t len)
{
	const struct mfd_mch2022_rp2040_config *config = dev->config;

	return i2c_burst_write_dt(&config->i2c, reg, buf, len);
}

uint8_t mfd_mch2022_rp2040_fw_version(const struct device *dev)
{
	const struct mfd_mch2022_rp2040_data *data = dev->data;

	return data->fw_ver;
}

static int mfd_mch2022_rp2040_init(const struct device *dev)
{
	const struct mfd_mch2022_rp2040_config *config = dev->config;
	struct mfd_mch2022_rp2040_data *data = dev->data;
	const uint8_t unlock = 0U;
	int ret;

	if (!i2c_is_ready_dt(&config->i2c)) {
		LOG_ERR_DEVICE_NOT_READY(config->i2c.bus);
		return -ENODEV;
	}

	ret = i2c_reg_read_byte_dt(&config->i2c, MCH2022_RP2040_FW_VER, &data->fw_ver);
	if (ret < 0) {
		LOG_ERR("Failed to read the firmware version: %d", ret);
		return ret;
	}

	if (data->fw_ver == MCH2022_RP2040_FW_VER_BOOTLOADER) {
		LOG_ERR("RP2040 is in its bootloader");
		return -ENODEV;
	}

	LOG_DBG("RP2040 firmware 0x%02x", data->fw_ver);

	/* The badge's stock firmware sets the lock at every boot; the RP2040 keeps it */
	if (IS_ENABLED(CONFIG_MFD_MCH2022_RP2040_DOWNLOAD_RESET) &&
	    (data->fw_ver >= MCH2022_RP2040_FW_VER_RESET_LOCK)) {
		ret = mfd_mch2022_rp2040_write(dev, MCH2022_RP2040_RESET_LOCK, &unlock, 1U);
		if (ret < 0) {
			LOG_ERR("Failed to clear the reset lock: %d", ret);
			return ret;
		}
	}

	return 0;
}

#define MFD_MCH2022_RP2040_DEFINE(n)                                                               \
	static const struct mfd_mch2022_rp2040_config mfd_mch2022_rp2040_config_##n = {            \
		.i2c = I2C_DT_SPEC_INST_GET(n),                                                    \
	};                                                                                         \
                                                                                                   \
	static struct mfd_mch2022_rp2040_data mfd_mch2022_rp2040_data_##n;                         \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(n, mfd_mch2022_rp2040_init, NULL, &mfd_mch2022_rp2040_data_##n,      \
			      &mfd_mch2022_rp2040_config_##n, POST_KERNEL,                         \
			      CONFIG_MFD_INIT_PRIORITY, NULL);

DT_INST_FOREACH_STATUS_OKAY(MFD_MCH2022_RP2040_DEFINE)
