// SPDX-License-Identifier: GPL-2.0
/*
 * Sony IMX241 Camera Sensor Driver for Intel IPU3
 * Samsung Galaxy Book 12 (SM-W720) Front-Facing Camera
 */

#include <linux/acpi.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio/driver.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <media/v4l2-cci.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>
#include <linux/unaligned.h>

#define IMX241_REG_MODE_SELECT		CCI_REG8(0x0100)
#define IMX241_MODE_STANDBY		0x00
#define IMX241_MODE_STREAMING		0x01

#define IMX241_REG_RESET		CCI_REG8(0x0103)

/* Chip ID */
#define IMX241_REG_CHIP_ID		CCI_REG16(0x0000)
#define IMX241_CHIP_ID			0x0241

/* Timings */
#define IMX241_VTS_DEFAULT		2280
#define IMX241_VTS_MAX			65535
#define IMX241_HTS_DEFAULT		1456

/* Exposure control */
#define IMX241_REG_EXPOSURE		CCI_REG16(0x0202)
#define IMX241_EXPOSURE_MIN		4
#define IMX241_EXPOSURE_STEP		1
#define IMX241_EXPOSURE_DEFAULT		0x0320
#define IMX241_EXPOSURE_MAX		(IMX241_VTS_MAX - 10)

/* Analog gain control (16-bit) */
#define IMX241_REG_ANALOG_GAIN		CCI_REG16(0x0204)
#define IMX241_ANA_GAIN_MIN		0
#define IMX241_ANA_GAIN_MAX		480
#define IMX241_ANA_GAIN_STEP		1
#define IMX241_ANA_GAIN_DEFAULT		0x0080

/* Test pattern */
#define IMX241_REG_TEST_PATTERN		CCI_REG16(0x0600)

/* Orientation */
#define REG_MIRROR_FLIP_CONTROL		CCI_REG8(0x0101)

#define IMX241_NATIVE_WIDTH		2592U
#define IMX241_NATIVE_HEIGHT		1944U

static const s64 link_freq_menu_items[] = {
	248976000,
	497952000,
};

static const char * const imx241_test_pattern_menu[] = {
	"Disabled",
	"Solid Colour",
	"100% Colour Bars",
	"Fade to Grey Colour Bars",
	"PN9",
};

struct imx241_mode {
	u32 width;
	u32 height;
	u32 vts;
	u32 hts;
	struct v4l2_rect crop;
};

static const struct imx241_mode supported_modes[] = {
	{
		.width = 2592,
		.height = 1944,
		.vts = 2280,
		.hts = 1456,
		.crop = {
			.left = 0,
			.top = 0,
			.width = 2592,
			.height = 1944,
		},
	},
	{
		.width = 1296,
		.height = 972,
		.vts = 1140,
		.hts = 1456,
		.crop = {
			.left = 0,
			.top = 0,
			.width = 2592,
			.height = 1944,
		},
	},
};

struct imx241 {
	struct v4l2_subdev sd;
	struct media_pad pad;
	struct regmap *regmap;
	struct clk *clk;
	struct gpio_desc *reset_gpio;
	struct gpio_desc *powerdown_gpio;

	struct v4l2_ctrl_handler ctrl_handler;
	struct v4l2_ctrl *link_freq;
	struct v4l2_ctrl *pixel_rate;
	struct v4l2_ctrl *vblank;
	struct v4l2_ctrl *hblank;
	struct v4l2_ctrl *exposure;
	struct v4l2_ctrl *analog_gain;
	struct v4l2_ctrl *hflip;
	struct v4l2_ctrl *vflip;
	struct v4l2_ctrl *test_pattern;

	struct mutex mutex;
	bool streaming;
	const struct imx241_mode *cur_mode;
};

static inline struct imx241 *to_imx241(struct v4l2_subdev *_sd)
{
	return container_of(_sd, struct imx241, sd);
}

static u32 imx241_get_format_code(const struct imx241 *imx241)
{
	unsigned int i;
	static const u32 codes[4] = {
		MEDIA_BUS_FMT_SRGGB10_1X10,
		MEDIA_BUS_FMT_SGRBG10_1X10,
		MEDIA_BUS_FMT_SGBRG10_1X10,
		MEDIA_BUS_FMT_SBGGR10_1X10,
	};

	i = (imx241->vflip->val ? 2 : 0) |
	    (imx241->hflip->val ? 1 : 0);

	return codes[i];
}

static int imx241_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct imx241 *imx241 = to_imx241(sd);
	struct v4l2_mbus_framefmt *try_fmt =
		v4l2_subdev_state_get_format(fh->state, 0);
	struct v4l2_rect *try_crop;

	mutex_lock(&imx241->mutex);

	try_fmt->width = supported_modes[0].width;
	try_fmt->height = supported_modes[0].height;
	try_fmt->code = imx241_get_format_code(imx241);
	try_fmt->field = V4L2_FIELD_NONE;

	try_crop = v4l2_subdev_state_get_crop(fh->state, 0);
	try_crop->left = 0;
	try_crop->top = 0;
	try_crop->width = supported_modes[0].width;
	try_crop->height = supported_modes[0].height;

	mutex_unlock(&imx241->mutex);

	return 0;
}

static int imx241_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct imx241 *imx241 = container_of(ctrl->handler,
					     struct imx241, ctrl_handler);
	int ret = 0;

	if (!pm_runtime_get_if_in_use(imx241->sd.dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		ret = cci_write(imx241->regmap, IMX241_REG_EXPOSURE,
				ctrl->val, NULL);
		break;
	case V4L2_CID_ANALOGUE_GAIN:
		ret = cci_write(imx241->regmap, IMX241_REG_ANALOG_GAIN,
				ctrl->val, NULL);
		break;
	case V4L2_CID_VBLANK:
		ret = cci_write(imx241->regmap, CCI_REG16(0x0340),
				imx241->cur_mode->height + ctrl->val, NULL);
		break;
	case V4L2_CID_TEST_PATTERN:
		ret = cci_write(imx241->regmap, IMX241_REG_TEST_PATTERN,
				ctrl->val, NULL);
		break;
	case V4L2_CID_HFLIP:
	case V4L2_CID_VFLIP:
		ret = cci_write(imx241->regmap, REG_MIRROR_FLIP_CONTROL,
				(imx241->hflip->val ? 1 : 0) |
				(imx241->vflip->val ? 2 : 0), NULL);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	pm_runtime_put(imx241->sd.dev);

	return ret;
}

static const struct v4l2_ctrl_ops imx241_ctrl_ops = {
	.s_ctrl = imx241_set_ctrl,
};

static int imx241_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	struct imx241 *imx241 = to_imx241(sd);

	if (code->index != 0)
		return -EINVAL;

	code->code = imx241_get_format_code(imx241);

	return 0;
}

static int imx241_enum_frame_size(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *sd_state,
				  struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->index >= ARRAY_SIZE(supported_modes))
		return -EINVAL;

	fse->min_width = supported_modes[fse->index].width;
	fse->max_width = fse->min_width;
	fse->min_height = supported_modes[fse->index].height;
	fse->max_height = fse->min_height;

	return 0;
}

static void imx241_update_pad_format(const struct imx241 *imx241,
				     const struct imx241_mode *mode,
				     struct v4l2_subdev_format *fmt)
{
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.code = imx241_get_format_code(imx241);
	fmt->format.field = V4L2_FIELD_NONE;
}

static int imx241_get_pad_format(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_format *fmt)
{
	struct imx241 *imx241 = to_imx241(sd);

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		struct v4l2_mbus_framefmt *try_fmt =
			v4l2_subdev_state_get_format(sd_state, fmt->pad);
		fmt->format = *try_fmt;
	} else {
		imx241_update_pad_format(imx241, imx241->cur_mode, fmt);
	}

	return 0;
}

static int imx241_set_pad_format(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_format *fmt)
{
	struct imx241 *imx241 = to_imx241(sd);
	const struct imx241_mode *mode;

	mutex_lock(&imx241->mutex);

	mode = &supported_modes[0];
	if (fmt->format.width <= 1296 && fmt->format.height <= 972)
		mode = &supported_modes[1];

	imx241_update_pad_format(imx241, mode, fmt);

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		*v4l2_subdev_state_get_format(sd_state, fmt->pad) = fmt->format;
	} else {
		imx241->cur_mode = mode;
		__v4l2_ctrl_s_ctrl(imx241->vblank, mode->vts - mode->height);
		__v4l2_ctrl_s_ctrl(imx241->hblank, mode->hts - mode->width);
	}

	mutex_unlock(&imx241->mutex);

	return 0;
}

static int imx241_get_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *sd_state,
				struct v4l2_subdev_selection *sel)
{
	struct imx241 *imx241 = to_imx241(sd);

	switch (sel->target) {
	case V4L2_SEL_TGT_CROP:
		mutex_lock(&imx241->mutex);
		sel->r = imx241->cur_mode->crop;
		mutex_unlock(&imx241->mutex);
		return 0;
	case V4L2_SEL_TGT_NATIVE_SIZE:
	case V4L2_SEL_TGT_CROP_DEFAULT:
	case V4L2_SEL_TGT_CROP_BOUNDS:
		sel->r.left = 0;
		sel->r.top = 0;
		sel->r.width = IMX241_NATIVE_WIDTH;
		sel->r.height = IMX241_NATIVE_HEIGHT;
		return 0;
	default:
		return -EINVAL;
	}
}

static int imx241_start_streaming(struct imx241 *imx241)
{
	int ret;

	/* Setup sensor dimensions */
	ret = cci_write(imx241->regmap, CCI_REG16(0x034c), imx241->cur_mode->width, NULL);
	ret |= cci_write(imx241->regmap, CCI_REG16(0x034e), imx241->cur_mode->height, NULL);
	ret |= cci_write(imx241->regmap, CCI_REG16(0x0340), imx241->cur_mode->vts, NULL);
	ret |= cci_write(imx241->regmap, CCI_REG16(0x0342), imx241->cur_mode->hts, NULL);

	/* Apply controls */
	ret |= __v4l2_ctrl_handler_setup(&imx241->ctrl_handler);

	/* Enable streaming */
	ret |= cci_write(imx241->regmap, IMX241_REG_MODE_SELECT,
			 IMX241_MODE_STREAMING, NULL);

	if (ret) {
		dev_err(imx241->sd.dev, "Failed to start streaming\n");
		return ret;
	}

	return 0;
}

static int imx241_stop_streaming(struct imx241 *imx241)
{
	return cci_write(imx241->regmap, IMX241_REG_MODE_SELECT,
			 IMX241_MODE_STANDBY, NULL);
}

static int imx241_set_stream(struct v4l2_subdev *sd, int enable)
{
	struct imx241 *imx241 = to_imx241(sd);
	int ret = 0;

	mutex_lock(&imx241->mutex);

	if (imx241->streaming == enable)
		goto unlock;

	if (enable) {
		ret = pm_runtime_resume_and_get(imx241->sd.dev);
		if (ret < 0)
			goto unlock;

		ret = imx241_start_streaming(imx241);
		if (ret) {
			pm_runtime_put(imx241->sd.dev);
			goto unlock;
		}
	} else {
		imx241_stop_streaming(imx241);
		pm_runtime_put(imx241->sd.dev);
	}

	imx241->streaming = enable;

unlock:
	mutex_unlock(&imx241->mutex);
	return ret;
}


static int match_first_gpio(struct gpio_chip *chip, const void *data)
{
	return 1;
}

static void imx241_hardware_power(bool enable)
{
	struct gpio_device *gdev;
	struct gpio_chip *chip;

	gdev = gpio_device_find(NULL, match_first_gpio);
	if (!gdev)
		return;
	chip = gpio_device_get_chip(gdev);
	if (!chip)
		return;

	if (enable) {
		chip->direction_output(chip, 72, 1);
		chip->direction_output(chip, 75, 1);
		chip->direction_output(chip, 85, 1);
		chip->direction_output(chip, 143, 1);
		msleep(20);
		chip->direction_output(chip, 120, 1);
		msleep(20);
	} else {
		chip->direction_output(chip, 120, 0);
		chip->direction_output(chip, 143, 0);
		chip->direction_output(chip, 85, 0);
		chip->direction_output(chip, 75, 0);
		chip->direction_output(chip, 72, 0);
	}
}
static int imx241_power_on(struct imx241 *imx241)
{
	struct device *dev = imx241->sd.dev;
	int ret;

	imx241_hardware_power(true);

	if (imx241->clk) {
		ret = clk_prepare_enable(imx241->clk);
		if (ret) {
			dev_err(dev, "Failed to enable clock: %d\n", ret);
			return ret;
		}
	}

	if (imx241->reset_gpio)
		gpiod_set_value_cansleep(imx241->reset_gpio, 1);
	if (imx241->powerdown_gpio)
		gpiod_set_value_cansleep(imx241->powerdown_gpio, 1);

	usleep_range(5000, 10000);

	return 0;
}

static int imx241_power_off(struct imx241 *imx241)
{
	if (imx241->reset_gpio)
		gpiod_set_value_cansleep(imx241->reset_gpio, 0);
	if (imx241->powerdown_gpio)
		gpiod_set_value_cansleep(imx241->powerdown_gpio, 0);
	if (imx241->clk)
		clk_disable_unprepare(imx241->clk);

	imx241_hardware_power(false);

	return 0;
}

static int imx241_runtime_resume(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	return imx241_power_on(to_imx241(sd));
}

static int imx241_runtime_suspend(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	return imx241_power_off(to_imx241(sd));
}

static const struct v4l2_subdev_video_ops imx241_video_ops = {
	.s_stream = imx241_set_stream,
};

static const struct v4l2_subdev_pad_ops imx241_pad_ops = {
	.enum_mbus_code = imx241_enum_mbus_code,
	.enum_frame_size = imx241_enum_frame_size,
	.get_fmt = imx241_get_pad_format,
	.set_fmt = imx241_set_pad_format,
	.get_selection = imx241_get_selection,
};

static const struct v4l2_subdev_ops imx241_subdev_ops = {
	.video = &imx241_video_ops,
	.pad = &imx241_pad_ops,
};

static const struct media_entity_operations imx241_subdev_entity_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

static const struct v4l2_subdev_internal_ops imx241_internal_ops = {
	.open = imx241_open,
};

static int imx241_init_controls(struct imx241 *imx241)
{
	struct v4l2_ctrl_handler *ctrl_hdlr = &imx241->ctrl_handler;
	int ret;

	ret = v4l2_ctrl_handler_init(ctrl_hdlr, 10);
	if (ret)
		return ret;

	mutex_init(&imx241->mutex);
	ctrl_hdlr->lock = &imx241->mutex;

	imx241->link_freq = v4l2_ctrl_new_int_menu(ctrl_hdlr,
						  &imx241_ctrl_ops,
						  V4L2_CID_LINK_FREQ,
						  ARRAY_SIZE(link_freq_menu_items) - 1,
						  0, link_freq_menu_items);
	if (imx241->link_freq)
		imx241->link_freq->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	/* 99590400 pixels/sec */
	imx241->pixel_rate = v4l2_ctrl_new_std(ctrl_hdlr, &imx241_ctrl_ops,
					       V4L2_CID_PIXEL_RATE, 0,
					       99590400, 1, 99590400);

	imx241->vblank = v4l2_ctrl_new_std(ctrl_hdlr, &imx241_ctrl_ops,
					   V4L2_CID_VBLANK, 10,
					   IMX241_VTS_MAX - 1944, 1,
					   IMX241_VTS_DEFAULT - 1944);

	imx241->hblank = v4l2_ctrl_new_std(ctrl_hdlr, &imx241_ctrl_ops,
					   V4L2_CID_HBLANK,
					   IMX241_HTS_DEFAULT - 2592,
					   IMX241_HTS_DEFAULT - 2592, 1,
					   IMX241_HTS_DEFAULT - 2592);
	if (imx241->hblank)
		imx241->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	imx241->exposure = v4l2_ctrl_new_std(ctrl_hdlr, &imx241_ctrl_ops,
					     V4L2_CID_EXPOSURE,
					     IMX241_EXPOSURE_MIN,
					     IMX241_EXPOSURE_MAX,
					     IMX241_EXPOSURE_STEP,
					     IMX241_EXPOSURE_DEFAULT);

	imx241->analog_gain = v4l2_ctrl_new_std(ctrl_hdlr, &imx241_ctrl_ops,
						V4L2_CID_ANALOGUE_GAIN,
						IMX241_ANA_GAIN_MIN,
						IMX241_ANA_GAIN_MAX,
						IMX241_ANA_GAIN_STEP,
						IMX241_ANA_GAIN_DEFAULT);

	imx241->hflip = v4l2_ctrl_new_std(ctrl_hdlr, &imx241_ctrl_ops,
					  V4L2_CID_HFLIP, 0, 1, 1, 0);
	imx241->vflip = v4l2_ctrl_new_std(ctrl_hdlr, &imx241_ctrl_ops,
					  V4L2_CID_VFLIP, 0, 1, 1, 0);

	imx241->test_pattern = v4l2_ctrl_new_std_menu_items(ctrl_hdlr,
				&imx241_ctrl_ops, V4L2_CID_TEST_PATTERN,
				ARRAY_SIZE(imx241_test_pattern_menu) - 1,
				0, 0, imx241_test_pattern_menu);

	v4l2_ctrl_new_std(ctrl_hdlr, &imx241_ctrl_ops,
			  V4L2_CID_CAMERA_SENSOR_ROTATION, 0, 0, 1, 0);

	if (ctrl_hdlr->error) {
		ret = ctrl_hdlr->error;
		v4l2_ctrl_handler_free(ctrl_hdlr);
		mutex_destroy(&imx241->mutex);
		return ret;
	}

	imx241->sd.ctrl_handler = ctrl_hdlr;

	return 0;
}

static int imx241_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct imx241 *imx241;
	u64 val;
	int ret;

	imx241 = devm_kzalloc(dev, sizeof(*imx241), GFP_KERNEL);
	if (!imx241)
		return -ENOMEM;

	/* Report subdev name matching standard Sony helper in libcamera */
	v4l2_i2c_subdev_init(&imx241->sd, client, &imx241_subdev_ops);
	strscpy(imx241->sd.name, "imx258 5-0037", sizeof(imx241->sd.name));
	imx241->sd.internal_ops = &imx241_internal_ops;
	imx241->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	imx241->sd.entity.ops = &imx241_subdev_entity_ops;
	imx241->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;

	imx241->cur_mode = &supported_modes[0];

	imx241->regmap = devm_cci_regmap_init_i2c(client, 16);
	if (IS_ERR(imx241->regmap))
		return PTR_ERR(imx241->regmap);

	/* Clock */
	imx241->clk = devm_clk_get_optional(dev, NULL);
	if (IS_ERR(imx241->clk))
		return dev_err_probe(dev, PTR_ERR(imx241->clk),
				     "Failed to get clock\n");

	/* GPIOs */
	imx241->reset_gpio = devm_gpiod_get_optional(dev, "reset",
						     GPIOD_OUT_LOW);
	if (IS_ERR(imx241->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(imx241->reset_gpio),
				     "Failed to get reset GPIO\n");

	imx241->powerdown_gpio = devm_gpiod_get_optional(dev, "powerdown",
							 GPIOD_OUT_LOW);
	if (IS_ERR(imx241->powerdown_gpio))
		return dev_err_probe(dev, PTR_ERR(imx241->powerdown_gpio),
				     "Failed to get powerdown GPIO\n");

	/* Power on to identify chip */
	ret = imx241_power_on(imx241);
	if (ret)
		return ret;

	ret = cci_read(imx241->regmap, IMX241_REG_CHIP_ID, &val, NULL);
	if (ret || val != IMX241_CHIP_ID) {
		dev_err(dev, "Chip ID mismatch: expected 0x%04x, read 0x%04llx (ret=%d)\n",
			IMX241_CHIP_ID, val, ret);
		imx241_power_off(imx241);
		return -ENODEV;
	}

	dev_info(dev, "Sony IMX241 detected successfully (ID: 0x%04llx)\n", val);

	ret = imx241_init_controls(imx241);
	if (ret) {
		imx241_power_off(imx241);
		return ret;
	}

	imx241->pad.flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_pads_init(&imx241->sd.entity, 1, &imx241->pad);
	if (ret)
		goto err_free_ctrls;

	/* Register subdev */
	ret = v4l2_async_register_subdev(&imx241->sd);
	if (ret < 0) {
		dev_err(dev, "Failed to register subdev: %d\n", ret);
		goto err_clean_entity;
	}

	/* Runtime PM */
	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	pm_runtime_idle(dev);

	dev_info(dev, "Sony IMX241 probe completed successfully\n");

	return 0;

err_clean_entity:
	media_entity_cleanup(&imx241->sd.entity);
err_free_ctrls:
	v4l2_ctrl_handler_free(&imx241->ctrl_handler);
	mutex_destroy(&imx241->mutex);
	imx241_power_off(imx241);
	return ret;
}

static void imx241_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx241 *imx241 = to_imx241(sd);

	v4l2_async_unregister_subdev(sd);
	media_entity_cleanup(&sd->entity);
	v4l2_ctrl_handler_free(&imx241->ctrl_handler);
	mutex_destroy(&imx241->mutex);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		imx241_power_off(imx241);
	pm_runtime_set_suspended(&client->dev);
}

static const struct dev_pm_ops imx241_pm_ops = {
	SET_RUNTIME_PM_OPS(imx241_runtime_suspend, imx241_runtime_resume, NULL)
};

static const struct acpi_device_id imx241_acpi_ids[] = {
	{ "INT347F" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(acpi, imx241_acpi_ids);

static struct i2c_driver imx241_driver = {
	.driver = {
		.name = "imx241",
		.pm = &imx241_pm_ops,
		.acpi_match_table = imx241_acpi_ids,
	},
	.probe = imx241_probe,
	.remove = imx241_remove,
};

module_i2c_driver(imx241_driver);

MODULE_AUTHOR("Frank & DeepMind Antigravity");
MODULE_DESCRIPTION("Sony IMX241 Sensor Driver for Galaxy Book 12");
MODULE_LICENSE("GPL");
