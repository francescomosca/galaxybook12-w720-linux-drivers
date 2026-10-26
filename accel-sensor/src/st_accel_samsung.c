// SPDX-License-Identifier: GPL-2.0-only
/*
 * Samsung Galaxy Book 12 (SM-W720) Accelerometer Driver
 * Maps ACPI ID "SAM0201" to STMicroelectronics LIS2HH12 (st_accel)
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/acpi.h>
#include <linux/iio/iio.h>
#include <linux/iio/common/st_sensors_i2c.h>
#include "st_accel.h"

static const struct acpi_device_id st_accel_samsung_acpi_match[] = {
	{ "SAM0201", (kernel_ulong_t)LIS2HH12_ACCEL_DEV_NAME },
	{ }
};
MODULE_DEVICE_TABLE(acpi, st_accel_samsung_acpi_match);

static const struct i2c_device_id st_accel_samsung_id_table[] = {
	{ LIS2HH12_ACCEL_DEV_NAME, 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, st_accel_samsung_id_table);

static int st_accel_samsung_probe(struct i2c_client *client)
{
	const struct st_sensor_settings *settings;
	struct st_sensor_data *adata;
	struct iio_dev *indio_dev;
	int ret;

	st_sensors_dev_name_probe(&client->dev, client->name, sizeof(client->name));

	settings = st_accel_get_settings(client->name);
	if (!settings) {
		dev_err(&client->dev, "device name %s not recognized.\n", client->name);
		return -ENODEV;
	}

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*adata));
	if (!indio_dev)
		return -ENOMEM;

	adata = iio_priv(indio_dev);
	adata->sensor_settings = (struct st_sensor_settings *)settings;

	ret = st_sensors_i2c_configure(indio_dev, client);
	if (ret < 0)
		return ret;

	ret = st_sensors_power_enable(indio_dev);
	if (ret)
		return ret;

	return st_accel_common_probe(indio_dev);
}

static struct i2c_driver st_accel_samsung_driver = {
	.driver = {
		.name = "st-accel-samsung",
		.acpi_match_table = ACPI_PTR(st_accel_samsung_acpi_match),
	},
	.probe = st_accel_samsung_probe,
	.id_table = st_accel_samsung_id_table,
};
module_i2c_driver(st_accel_samsung_driver);

MODULE_AUTHOR("Frank");
MODULE_DESCRIPTION("Samsung Galaxy Book 12 Accelerometer Driver (SAM0201 -> LIS2HH12)");
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS("IIO_ST_SENSORS");
