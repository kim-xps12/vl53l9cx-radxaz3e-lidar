// SPDX-License-Identifier: GPL-2.0
/*
 * VL53L9CX CSI-2 stub sensor sub-device for Rockchip RK356x (Radxa ZERO 3E).
 *
 * The VL53L9CX is booted and configured from user space over I2C
 * (upstream/app/vl53l9_bringup.c). This driver only gives the Rockchip
 * capture pipeline
 *     csi2_dphy0 -> mipi_csi2 -> rkcif_mipi_lvds (VICAP) -> /dev/videoN
 * a sensor end point that answers what those drivers ask for:
 *   - pad format          : MEDIA_BUS_FMT_Y8_1X8 (CSI-2 data type 0x2A, RAW8)
 *   - get_mbus_config     : CSI-2 D-PHY, number of data lanes (from DT)
 *   - V4L2_CID_LINK_FREQ  : used by the D-PHY to pick its HS receive band
 *   - V4L2_CID_PIXEL_RATE
 *   - g_frame_interval    : rkcif refuses to start without it
 * It also starts the sensor reference clock (AP_CLK, FFC pin 18) when the
 * node has "clocks": the STEVAL-VL53L9 can ship with its on-board 12 MHz
 * oscillator disabled, and the sensor then needs exactly 12 MHz from the host.
 * Optional "xshut-gpios" (FFC pin 17): the sensor is held in reset while not
 * streaming (laser off), and every stream start cycles XSHUT low->high so the
 * user-space bring-up always finds it in READY_TO_BOOT. On the Radxa ZERO 3E
 * the sensor did not come up when XSHUT was released while the capture path
 * was idle (tested 3 times), and did every time inside s_stream (6/6). Without that property XSHUT is left alone (e.g. tied to
 * 3V3 with a jumper; then the GPIO must never be driven).
 * The sensor itself is started over I2C after the receiver has been armed.
 *
 * Idea taken from driver/dummy_cam.c of jonferran/vl53l9cx-pi-lidar (MIT),
 * rewritten for the Rockchip BSP 6.1 media drivers.
 */
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <media/media-entity.h>
#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-subdev.h>

#define STUB_NAME		"vl53l9cx-stub"
/* binning 2: 100 bytes x (148 data lines + 1 status line) */
#define STUB_DEF_WIDTH		100
#define STUB_DEF_HEIGHT		149
#define STUB_MAX_SIZE		4096
#define STUB_DEF_LINK_FREQ	500000000LL	/* 1 Gbit/s per lane, DDR */
#define STUB_DEF_FPS		100
#define STUB_DEF_XVCLK		12000000	/* VL53L9CX reference clock */

static unsigned int fps = STUB_DEF_FPS;
module_param(fps, uint, 0644);
MODULE_PARM_DESC(fps, "frame rate reported to the capture driver");

struct vl53l9_stub {
	struct v4l2_subdev sd;
	struct media_pad pad;
	struct v4l2_ctrl_handler ctrls;
	struct v4l2_mbus_framefmt fmt;
	struct mutex lock;		/* protects fmt */
	struct clk *xvclk;		/* AP_CLK out of the SoC, optional */
	struct gpio_desc *xshut;	/* sensor XSHUT, optional, active high */
	int stream_count;		/* protected by lock */
	s64 link_freq[1];
	u32 lanes;
};

static inline struct vl53l9_stub *to_stub(struct v4l2_subdev *sd)
{
	return container_of(sd, struct vl53l9_stub, sd);
}

static void stub_fill_fmt(struct v4l2_mbus_framefmt *f, u32 w, u32 h)
{
	memset(f, 0, sizeof(*f));
	f->code = MEDIA_BUS_FMT_Y8_1X8;
	f->width = clamp_t(u32, w, 1, STUB_MAX_SIZE);
	f->height = clamp_t(u32, h, 1, STUB_MAX_SIZE);
	f->field = V4L2_FIELD_NONE;
	f->colorspace = V4L2_COLORSPACE_RAW;
}

static int stub_get_fmt(struct v4l2_subdev *sd,
			struct v4l2_subdev_state *state,
			struct v4l2_subdev_format *format)
{
	struct vl53l9_stub *s = to_stub(sd);

	if (format->pad != 0)
		return -EINVAL;
	if (format->which == V4L2_SUBDEV_FORMAT_TRY && state) {
		format->format = *v4l2_subdev_get_try_format(sd, state, 0);
		return 0;
	}
	mutex_lock(&s->lock);
	format->format = s->fmt;
	mutex_unlock(&s->lock);
	return 0;
}

static int stub_set_fmt(struct v4l2_subdev *sd,
			struct v4l2_subdev_state *state,
			struct v4l2_subdev_format *format)
{
	struct vl53l9_stub *s = to_stub(sd);
	struct v4l2_mbus_framefmt f;

	if (format->pad != 0)
		return -EINVAL;
	stub_fill_fmt(&f, format->format.width, format->format.height);
	format->format = f;
	if (format->which == V4L2_SUBDEV_FORMAT_TRY) {
		if (state)
			*v4l2_subdev_get_try_format(sd, state, 0) = f;
		return 0;
	}
	mutex_lock(&s->lock);
	s->fmt = f;
	mutex_unlock(&s->lock);
	return 0;
}

static int stub_enum_mbus_code(struct v4l2_subdev *sd,
			       struct v4l2_subdev_state *state,
			       struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->pad != 0 || code->index != 0)
		return -EINVAL;
	code->code = MEDIA_BUS_FMT_Y8_1X8;
	return 0;
}

static int stub_enum_frame_size(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *state,
				struct v4l2_subdev_frame_size_enum *fse)
{
	struct vl53l9_stub *s = to_stub(sd);

	if (fse->pad != 0 || fse->index != 0 ||
	    fse->code != MEDIA_BUS_FMT_Y8_1X8)
		return -EINVAL;
	mutex_lock(&s->lock);
	fse->min_width = fse->max_width = s->fmt.width;
	fse->min_height = fse->max_height = s->fmt.height;
	mutex_unlock(&s->lock);
	return 0;
}

static int stub_get_mbus_config(struct v4l2_subdev *sd, unsigned int pad,
				struct v4l2_mbus_config *cfg)
{
	struct vl53l9_stub *s = to_stub(sd);

	memset(cfg, 0, sizeof(*cfg));
	cfg->type = V4L2_MBUS_CSI2_DPHY;
	cfg->bus.mipi_csi2.num_data_lanes = s->lanes;
	return 0;
}

static int stub_g_frame_interval(struct v4l2_subdev *sd,
				 struct v4l2_subdev_frame_interval *fi)
{
	fi->interval.numerator = 1;
	fi->interval.denominator = fps ? fps : STUB_DEF_FPS;
	return 0;
}

/* XSHUT low -> high with the reference clock running: fresh READY_TO_BOOT */
static void stub_xshut_cycle(struct vl53l9_stub *s)
{
	gpiod_set_value_cansleep(s->xshut, 0);
	msleep(10);
	gpiod_set_value_cansleep(s->xshut, 1);
	msleep(200);	/* boot settle, as in the Pi 5 port */
}

static int stub_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct vl53l9_stub *s = to_stub(sd);

	dev_dbg(sd->dev, "s_stream(%d)\n", enable);
	if (!s->xshut)
		return 0;
	mutex_lock(&s->lock);
	if (enable) {
		if (s->stream_count++ == 0)
			stub_xshut_cycle(s);
	} else if (s->stream_count > 0 && --s->stream_count == 0) {
		gpiod_set_value_cansleep(s->xshut, 0);
	}
	mutex_unlock(&s->lock);
	return 0;
}

static const struct v4l2_subdev_pad_ops stub_pad_ops = {
	.enum_mbus_code = stub_enum_mbus_code,
	.enum_frame_size = stub_enum_frame_size,
	.get_fmt = stub_get_fmt,
	.set_fmt = stub_set_fmt,
	.get_mbus_config = stub_get_mbus_config,
};

static const struct v4l2_subdev_video_ops stub_video_ops = {
	.s_stream = stub_s_stream,
	.g_frame_interval = stub_g_frame_interval,
};

static const struct v4l2_subdev_ops stub_ops = {
	.pad = &stub_pad_ops,
	.video = &stub_video_ops,
};

static int stub_parse_endpoint(struct device *dev, struct vl53l9_stub *s)
{
	struct v4l2_fwnode_endpoint ep = { .bus_type = V4L2_MBUS_CSI2_DPHY };
	struct fwnode_handle *epn;
	int ret;

	s->lanes = 1;
	s->link_freq[0] = STUB_DEF_LINK_FREQ;

	epn = fwnode_graph_get_next_endpoint(dev_fwnode(dev), NULL);
	if (!epn) {
		dev_err(dev, "no endpoint in the device tree node\n");
		return -EINVAL;
	}
	ret = v4l2_fwnode_endpoint_alloc_parse(epn, &ep);
	fwnode_handle_put(epn);
	if (ret) {
		dev_err(dev, "cannot parse the endpoint (%d)\n", ret);
		return ret;
	}
	if (ep.bus.mipi_csi2.num_data_lanes)
		s->lanes = ep.bus.mipi_csi2.num_data_lanes;
	if (ep.nr_of_link_frequencies && ep.link_frequencies[0])
		s->link_freq[0] = ep.link_frequencies[0];
	v4l2_fwnode_endpoint_free(&ep);
	return 0;
}

static void stub_clk_off(void *data)
{
	clk_disable_unprepare(data);
}

static int stub_start_xvclk(struct device *dev, struct vl53l9_stub *s)
{
	u32 want = STUB_DEF_XVCLK;
	unsigned long got;
	int ret;

	s->xvclk = devm_clk_get_optional(dev, "xvclk");
	if (IS_ERR(s->xvclk))
		return dev_err_probe(dev, PTR_ERR(s->xvclk), "xvclk\n");
	if (!s->xvclk)
		return 0;	/* board has its own oscillator */

	of_property_read_u32(dev->of_node, "clock-frequency", &want);
	if (clk_get_rate(s->xvclk) != want) {
		ret = clk_set_rate(s->xvclk, want);
		if (ret)
			return dev_err_probe(dev, ret, "cannot set xvclk to %u Hz\n", want);
	}
	got = clk_get_rate(s->xvclk);
	if (got != want) {
		/* the sensor boots but faults when ranging at e.g. 10.67/12.5 MHz */
		dev_err(dev, "xvclk is %lu Hz, need exactly %u Hz\n", got, want);
		return -EINVAL;
	}
	ret = clk_prepare_enable(s->xvclk);
	if (ret)
		return dev_err_probe(dev, ret, "cannot enable xvclk\n");
	ret = devm_add_action_or_reset(dev, stub_clk_off, s->xvclk);
	if (ret)
		return ret;
	dev_info(dev, "reference clock on: %lu Hz\n", got);
	return 0;
}

static int stub_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct vl53l9_stub *s;
	struct v4l2_ctrl *ctrl;
	u64 pixel_rate;
	int ret;

	s = devm_kzalloc(dev, sizeof(*s), GFP_KERNEL);
	if (!s)
		return -ENOMEM;
	mutex_init(&s->lock);

	ret = stub_parse_endpoint(dev, s);
	if (ret)
		return ret;

	/* hold the sensor in reset (if we own XSHUT) until the clock runs */
	s->xshut = devm_gpiod_get_optional(dev, "xshut", GPIOD_OUT_LOW);
	if (IS_ERR(s->xshut))
		return dev_err_probe(dev, PTR_ERR(s->xshut), "xshut gpio\n");

	ret = stub_start_xvclk(dev, s);
	if (ret)
		return ret;
	if (s->xshut)
		dev_info(dev, "XSHUT driven by GPIO: low until a stream starts\n");

	v4l2_subdev_init(&s->sd, &stub_ops);
	s->sd.dev = dev;
	s->sd.owner = THIS_MODULE;
	s->sd.fwnode = dev_fwnode(dev);
	s->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	strscpy(s->sd.name, STUB_NAME, sizeof(s->sd.name));
	v4l2_set_subdevdata(&s->sd, s);

	stub_fill_fmt(&s->fmt, STUB_DEF_WIDTH, STUB_DEF_HEIGHT);

	/* RAW8: one pixel = one byte = 8 bits; DDR: 2 bits per lane per clock */
	pixel_rate = div_u64((u64)s->link_freq[0] * 2 * s->lanes, 8);

	v4l2_ctrl_handler_init(&s->ctrls, 2);
	ctrl = v4l2_ctrl_new_int_menu(&s->ctrls, NULL, V4L2_CID_LINK_FREQ,
				      0, 0, s->link_freq);
	if (ctrl)
		ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;
	v4l2_ctrl_new_std(&s->ctrls, NULL, V4L2_CID_PIXEL_RATE,
			  pixel_rate, pixel_rate, 1, pixel_rate);
	if (s->ctrls.error) {
		ret = s->ctrls.error;
		dev_err(dev, "control init failed (%d)\n", ret);
		goto err_ctrls;
	}
	s->sd.ctrl_handler = &s->ctrls;

	s->pad.flags = MEDIA_PAD_FL_SOURCE;
	s->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&s->sd.entity, 1, &s->pad);
	if (ret)
		goto err_ctrls;

	platform_set_drvdata(pdev, s);

	ret = v4l2_async_register_subdev(&s->sd);
	if (ret) {
		dev_err(dev, "async register failed (%d)\n", ret);
		goto err_entity;
	}
	dev_info(dev, "VL53L9CX stub: %u lane(s), link %lld Hz, %ux%u Y8\n",
		 s->lanes, s->link_freq[0], s->fmt.width, s->fmt.height);
	return 0;

err_entity:
	media_entity_cleanup(&s->sd.entity);
err_ctrls:
	v4l2_ctrl_handler_free(&s->ctrls);
	mutex_destroy(&s->lock);
	return ret;
}

static int stub_remove(struct platform_device *pdev)
{
	struct vl53l9_stub *s = platform_get_drvdata(pdev);

	if (s->xshut)
		gpiod_set_value_cansleep(s->xshut, 0);	/* sensor off */
	v4l2_async_unregister_subdev(&s->sd);
	media_entity_cleanup(&s->sd.entity);
	v4l2_ctrl_handler_free(&s->ctrls);
	mutex_destroy(&s->lock);
	return 0;
}

static const struct of_device_id stub_of_match[] = {
	{ .compatible = "st,vl53l9cx-csi2-stub" },
	{ }
};
MODULE_DEVICE_TABLE(of, stub_of_match);

static struct platform_driver stub_driver = {
	.driver = {
		.name = "vl53l9cx_stub",
		.of_match_table = stub_of_match,
	},
	.probe = stub_probe,
	.remove = stub_remove,
};
module_platform_driver(stub_driver);

MODULE_DESCRIPTION("VL53L9CX CSI-2 stub sensor sub-device for Rockchip VICAP");
MODULE_LICENSE("GPL");
