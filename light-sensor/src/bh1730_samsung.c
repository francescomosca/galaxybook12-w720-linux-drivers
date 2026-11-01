// SPDX-License-Identifier: GPL-2.0-only
/*
 * Samsung Galaxy Book 12 (SM-W720) Ambient Light Sensor Driver
 * Maps ACPI ID "SAM0204" (ROHM BH1733 / BH1730FVC ALS on I2C at 0x29)
 * Exposes standard Linux IIO light illuminance interface for iio-sensor-proxy.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/acpi.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/pm.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>

#define BH1730_NAME             "bh1730_samsung"

#define SET_CMD                 0x80

/* Registers */
#define REG_CON                 0x00
#define REG_TIME                0x01
#define REG_INT                 0x02
#define REG_TH_LL               0x03
#define REG_TH_LH               0x04
#define REG_TH_HL               0x05
#define REG_TH_HH               0x06
#define REG_GAIN                0x07
#define REG_ID                  0x12
#define REG_DATA0L              0x14
#define REG_DATA0H              0x15
#define REG_DATA1L              0x16
#define REG_DATA1H              0x17

/* CONTROL register bits */
#define CTL_POWER_ON            0x01
#define CTL_ADC_EN              0x02
#define CTL_ADC_VALID           0x10

/* GAIN codes */
#define GAIN_X1                 0x00
#define GAIN_X2                 0x01
#define GAIN_X64                0x02
#define GAIN_X128               0x03

struct bh1730_chip {
	struct i2c_client *client;
	struct mutex lock;
	u8 itime;
	u8 gain;
};

/* Read single byte using atomic i2c_transfer (Repeated START) */
static int bh1730_read_byte(struct i2c_client *client, u8 reg, u8 *val)
{
	u8 cmd = SET_CMD | reg;
	struct i2c_msg msgs[2] = {
		{
			.addr = client->addr,
			.flags = 0,
			.len = 1,
			.buf = &cmd,
		},
		{
			.addr = client->addr,
			.flags = I2C_M_RD,
			.len = 1,
			.buf = val,
		}
	};
	int ret = i2c_transfer(client->adapter, msgs, 2);

	if (ret < 0)
		return ret;
	if (ret != 2)
		return -EIO;

	return 0;
}

/* Write single byte using raw i2c_master_send */
static int bh1730_write_byte(struct i2c_client *client, u8 reg, u8 val)
{
	u8 buf[2];
	int ret;

	buf[0] = SET_CMD | reg;
	buf[1] = val;

	ret = i2c_master_send(client, buf, 2);
	if (ret != 2)
		return ret < 0 ? ret : -EIO;

	return 0;
}

/* Burst read 4 data bytes with atomic i2c_transfer (sensor auto-increments address) */
static int bh1730_read_raw_data(struct i2c_client *client, u16 *data0, u16 *data1)
{
	u8 cmd = SET_CMD | REG_DATA0L;
	u8 buf[4];
	struct i2c_msg msgs[2] = {
		{
			.addr = client->addr,
			.flags = 0,
			.len = 1,
			.buf = &cmd,
		},
		{
			.addr = client->addr,
			.flags = I2C_M_RD,
			.len = 4,
			.buf = buf,
		}
	};
	int ret = i2c_transfer(client->adapter, msgs, 2);

	if (ret < 0)
		return ret;
	if (ret != 2)
		return -EIO;

	*data0 = (buf[1] << 8) | buf[0];
	*data1 = (buf[3] << 8) | buf[2];
	return 0;
}

static u32 bh1730_get_gain_mult(u8 gain_reg)
{
	switch (gain_reg & 0x03) {
	case GAIN_X1:
		return 1;
	case GAIN_X2:
		return 2;
	case GAIN_X64:
		return 64;
	case GAIN_X128:
		return 128;
	default:
		return 1;
	}
}

/*
 * Rohm BH1730 / BH1733 lux calculation formula
 */
static u32 bh1730_calculate_lux(u16 data0, u16 data1, u8 itime_reg, u8 gain_reg)
{
	const u32 scale = 1000;
	const u32 scale1026 = 102600;
	u32 itime_ms, gain, ratio;
	u32 k0_scaled, k1_scaled;
	s64 numerator;
	u64 denominator;

	if (data0 == 0)
		return 0;

	itime_ms = ((256 - itime_reg) * 27) / 10;
	if (itime_ms == 0)
		itime_ms = 100;

	gain = bh1730_get_gain_mult(gain_reg);
	ratio = ((u32)data1 * scale) / data0;

	if (ratio < 260) {
		k0_scaled = 1290;
		k1_scaled = 2733;
	} else if (ratio < 550) {
		k0_scaled = 795;
		k1_scaled = 859;
	} else if (ratio < 1090) {
		k0_scaled = 510;
		k1_scaled = 345;
	} else if (ratio < 2130) {
		k0_scaled = 276;
		k1_scaled = 130;
	} else {
		return 0;
	}

	numerator = (s64)data0 * k0_scaled - (s64)data1 * k1_scaled;
	if (numerator <= 0)
		return 0;

	denominator = (u64)gain * scale1026;
	if (denominator == 0)
		return 0;

	return (u32)div64_u64((u64)numerator * itime_ms, denominator);
}

/*
 * Dynamic reading with auto-gain to prevent 16-bit saturation under bright light
 */
static int bh1730_get_lux(struct bh1730_chip *chip, int *val)
{
	u16 d0 = 0, d1 = 0;
	u8 ctl;
	int ret, retries = 25;
	u32 itime_ms;

	mutex_lock(&chip->lock);

	/* Ensure continuous ADC mode is active */
	ret = bh1730_read_byte(chip->client, REG_CON, &ctl);
	if (ret < 0 || (ctl & (CTL_POWER_ON | CTL_ADC_EN)) != (CTL_POWER_ON | CTL_ADC_EN)) {
		bh1730_write_byte(chip->client, REG_CON, CTL_POWER_ON | CTL_ADC_EN);
		msleep(50);
	}

	/* Wait until ADC data is valid if not yet ready */
	while (retries-- > 0) {
		ret = bh1730_read_byte(chip->client, REG_CON, &ctl);
		if (ret == 0 && (ctl & CTL_ADC_VALID))
			break;
		msleep(10);
	}

	ret = bh1730_read_raw_data(chip->client, &d0, &d1);
	if (ret < 0) {
		mutex_unlock(&chip->lock);
		return ret;
	}

	itime_ms = ((256 - chip->itime) * 27) / 10;
	if (itime_ms == 0)
		itime_ms = 100;

	/*
	 * Auto-gain adjustment:
	 * Check both d0 and d1 to prevent saturation under direct sunlight or IR-heavy light.
	 * If saturated (> 60000) drop gain; if reading is very low (< 200) increase gain.
	 * Wait for full ADC integration cycle before reading updated values.
	 */
	if ((d0 > 60000 || d1 > 60000) && chip->gain > GAIN_X1) {
		chip->gain = GAIN_X1;
		bh1730_write_byte(chip->client, REG_GAIN, chip->gain);
		msleep(itime_ms + 15);
		bh1730_read_raw_data(chip->client, &d0, &d1);
	} else if (d0 < 200 && d1 < 200 && chip->gain < GAIN_X64) {
		chip->gain = GAIN_X64;
		bh1730_write_byte(chip->client, REG_GAIN, chip->gain);
		msleep(itime_ms + 15);
		bh1730_read_raw_data(chip->client, &d0, &d1);
	}

	*val = (int)bh1730_calculate_lux(d0, d1, chip->itime, chip->gain);

	mutex_unlock(&chip->lock);
	return 0;
}

static int bh1730_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val, int *val2, long mask)
{
	struct bh1730_chip *chip = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_PROCESSED:
		ret = bh1730_get_lux(chip, val);
		if (ret < 0)
			return ret;
		return IIO_VAL_INT;
	default:
		return -EINVAL;
	}
}

static const struct iio_info bh1730_info = {
	.read_raw = bh1730_read_raw,
};

static const struct iio_chan_spec bh1730_channels[] = {
	{
		.type = IIO_LIGHT,
		.info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED),
	},
};

static int bh1730_probe(struct i2c_client *client)
{
	struct bh1730_chip *chip;
	struct iio_dev *indio_dev;
	u8 part_id = 0;
	int ret;

	/* Read Part ID register (0x12) */
	ret = bh1730_read_byte(client, REG_ID, &part_id);
	if (ret < 0) {
		dev_err(&client->dev, "Failed to read Part ID from ALS at addr 0x%02x (err: %d)\n",
			client->addr, ret);
		return ret;
	}

	dev_info(&client->dev, "Detected ROHM BH1730/BH1733 ALS (Part ID: 0x%02x) on I2C addr 0x%02x\n",
		 part_id, client->addr);

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*chip));
	if (!indio_dev)
		return -ENOMEM;

	chip = iio_priv(indio_dev);
	chip->client = client;
	mutex_init(&chip->lock);

	/* Samsung DSDT default timing: 0xD3 = 120ms integration */
	chip->itime = 0xD3;
	/* Start with moderate Gain X2 */
	chip->gain = GAIN_X2;

	bh1730_write_byte(client, REG_TIME, chip->itime);
	bh1730_write_byte(client, REG_GAIN, chip->gain);

	/* Power ON and enable continuous ADC */
	ret = bh1730_write_byte(client, REG_CON, CTL_POWER_ON | CTL_ADC_EN);
	if (ret < 0)
		return ret;

	indio_dev->name = BH1730_NAME;
	indio_dev->info = &bh1730_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = bh1730_channels;
	indio_dev->num_channels = ARRAY_SIZE(bh1730_channels);

	i2c_set_clientdata(client, indio_dev);

	ret = devm_iio_device_register(&client->dev, indio_dev);
	if (ret < 0) {
		dev_err(&client->dev, "Failed to register IIO device: %d\n", ret);
		return ret;
	}

	dev_info(&client->dev, "Registered IIO ambient light sensor successfully\n");
	return 0;
}

static void bh1730_remove(struct i2c_client *client)
{
	/* Power Down sensor on unload */
	bh1730_write_byte(client, REG_CON, 0x00);
}

static int bh1730_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct iio_dev *indio_dev = i2c_get_clientdata(client);
	struct bh1730_chip *chip = iio_priv(indio_dev);

	mutex_lock(&chip->lock);
	/* Power Down chip during suspend */
	bh1730_write_byte(client, REG_CON, 0x00);
	mutex_unlock(&chip->lock);

	return 0;
}

static int bh1730_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct iio_dev *indio_dev = i2c_get_clientdata(client);
	struct bh1730_chip *chip = iio_priv(indio_dev);

	mutex_lock(&chip->lock);
	/* Restore timing and gain */
	bh1730_write_byte(client, REG_TIME, chip->itime);
	bh1730_write_byte(client, REG_GAIN, chip->gain);
	/* Power ON and re-enable continuous ADC */
	bh1730_write_byte(client, REG_CON, CTL_POWER_ON | CTL_ADC_EN);
	mutex_unlock(&chip->lock);

	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(bh1730_pm_ops, bh1730_suspend, bh1730_resume);

static const struct acpi_device_id bh1730_acpi_match[] = {
	{ "SAM0204", 0 },
	{ }
};
MODULE_DEVICE_TABLE(acpi, bh1730_acpi_match);

static const struct i2c_device_id bh1730_id[] = {
	{ "bh1730_samsung", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, bh1730_id);

static struct i2c_driver bh1730_driver = {
	.driver = {
		.name = BH1730_NAME,
		.pm = pm_sleep_ptr(&bh1730_pm_ops),
		.acpi_match_table = bh1730_acpi_match,
	},
	.probe = bh1730_probe,
	.remove = bh1730_remove,
	.id_table = bh1730_id,
};

module_i2c_driver(bh1730_driver);

MODULE_AUTHOR("Samsung Galaxy Book 12 Community");
MODULE_DESCRIPTION("ROHM BH1733 / BH1730 Ambient Light Sensor Driver for Samsung Galaxy Book 12");
MODULE_LICENSE("GPL");
