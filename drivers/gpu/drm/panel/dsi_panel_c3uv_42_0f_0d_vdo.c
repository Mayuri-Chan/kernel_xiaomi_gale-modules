// SPDX-License-Identifier: GPL-2.0

#include <linux/ctype.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>
#include <linux/regmap.h>
#include <drm/drm_connector.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

#include "dsi_panel_c3uv_bias.h"
#include "../mediatek/mediatek_v2/mi_disp_esd_check.h"
#include "../mediatek/mediatek_v2/mtk_drm_graphics_base.h"
#include "../mediatek/mediatek_v2/mtk_panel_ext.h"
#include "../mediatek/mediatek_v2/drm_notifier_odm.h"

bool lcd_reset_keep_high_4th;
EXPORT_SYMBOL(lcd_reset_keep_high_4th);
bool prox_lcd_reset_keep_high4;
EXPORT_SYMBOL(prox_lcd_reset_keep_high4);

void set_lcd_reset_gpio_keep_high_4th(bool enable)
{
	lcd_reset_keep_high_4th = enable;
}
EXPORT_SYMBOL(set_lcd_reset_gpio_keep_high_4th);

void set_prox_lcd_reset_gpio_keep_high4(bool enable)
{
	prox_lcd_reset_keep_high4 = enable;
}
EXPORT_SYMBOL(set_prox_lcd_reset_gpio_keep_high4);

struct lcm_cmd {
	u16 delay_ms;
	u8 len;
	u8 data[65];
};

static const struct lcm_cmd init_setting_vdo[] = {
	{ .len = 3, .data = { 0xf0, 0x5a, 0x59 } },
	{ .len = 3, .data = { 0xf1, 0xa5, 0xa6 } },
	{ .len = 9, .data = { 0xbb, 0x01, 0x02, 0x03, 0x0a, 0x04, 0x13, 0x14, 0x00 } },
	{ .len = 11, .data = { 0xc4, 0x04, 0x33, 0xb8, 0x49, 0x00, 0xbc, 0x00, 0xa0, 0x00, 0x09 } },
	{ .len = 3, .data = { 0x35, 0x00, 0x00 } },
	{ .len = 3, .data = { 0x53, 0x2c, 0x00 } },
	{ .len = 3, .data = { 0x5e, 0x00, 0x07 } },
	{ .len = 33, .data = { 0xe0, 0x0c, 0x00, 0xb0, 0x0c, 0x00, 0x06, 0x0f, 0x00, 0x0b, 0x04, 0x81, 0x04, 0x0f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1f, 0x0f, 0x09, 0x92, 0x48, 0x42, 0xe8, 0xff, 0x10, 0x00, 0x00, 0x00 } },
	{ .len = 3, .data = { 0x11, 0x00, 0x00 } },
	{ .delay_ms = 100 },
	{ .len = 3, .data = { 0x29, 0x00, 0x00 } },
	{ .delay_ms = 10 },
	{ .len = 3, .data = { 0x6d, 0x02, 0x00 } },
	{ .len = 7, .data = { 0xbd, 0xa1, 0xa2, 0x52, 0x2e, 0x00, 0x8f } },
};
static const struct lcm_cmd lcm_suspend_setting[] = {
	{ .len = 3, .data = { 0x6d, 0x25, 0x00 } },
	{ .len = 3, .data = { 0x28, 0x00, 0x00 } },
	{ .delay_ms = 10 },
	{ .len = 3, .data = { 0x10, 0x00, 0x00 } },
	{ .delay_ms = 120 },
	{ .len = 9, .data = { 0xbb, 0x01, 0x02, 0x03, 0x0a, 0x04, 0x13, 0x14, 0x00 } },
	{ .len = 3, .data = { 0xf1, 0x5a, 0x59 } },
	{ .len = 3, .data = { 0xf0, 0xa5, 0xa6 } },
};

struct lcm_desc {
	const char *name;
	const struct lcm_cmd *init;
	size_t init_count;
	const struct lcm_cmd *sleep;
	size_t sleep_count;
	struct drm_display_mode modes[2];
};

#define LCM_MODE(clock_khz, hfp, hsa, hbp, vfp, vsa, vbp) { \
	.clock = (clock_khz), .hdisplay = 720, \
	.hsync_start = 720 + (hfp), .hsync_end = 720 + (hfp) + (hsa), \
	.htotal = 720 + (hfp) + (hsa) + (hbp), .vdisplay = 1600, \
	.vsync_start = 1600 + (vfp), .vsync_end = 1600 + (vfp) + (vsa), \
	.vtotal = 1600 + (vfp) + (vsa) + (vbp) }

static const struct lcm_desc lcm_desc = {
	.name = "dsi_panel_c3uv_42_0f_0d_vdo",
	.init = init_setting_vdo,
	.init_count = ARRAY_SIZE(init_setting_vdo),
	.sleep = lcm_suspend_setting,
	.sleep_count = ARRAY_SIZE(lcm_suspend_setting),
	.modes = { LCM_MODE(146966, 60, 4, 56, 1280, 4, 32),
		   LCM_MODE(147117, 60, 4, 56, 310, 4, 32) },
};

struct lcm {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	const struct lcm_desc *desc;
	struct gpio_desc *reset, *enp, *enn;
	struct mtk_panel_params params;
	bool prepared;
};

static struct lcm *to_lcm(struct drm_panel *panel)
{
	return container_of(panel, struct lcm, panel);
}

static int lcm_write(struct lcm *ctx, const u8 *data, size_t len)
{
	int ret = mipi_dsi_dcs_write_buffer(ctx->dsi, data, len);

	return ret < 0 ? ret : 0;
}

static int lcm_push(struct lcm *ctx, const struct lcm_cmd *cmds,
		     size_t count)
{
	size_t i;
	int ret;

	for (i = 0; i < count; i++) {
		if (cmds[i].delay_ms) {
			if (cmds[i].delay_ms < 20)
				usleep_range(cmds[i].delay_ms * 1000,
					cmds[i].delay_ms * 1000 + 1000);
			else
				msleep(cmds[i].delay_ms);
			continue;
		}
		ret = lcm_write(ctx, cmds[i].data, cmds[i].len);
		if (ret)
			return ret;
	}
	return 0;
}

static void lcm_power_off(struct lcm *ctx)
{
	gpiod_set_value_cansleep(ctx->enn, 0);
	usleep_range(2000, 3000);
	gpiod_set_value_cansleep(ctx->enp, 0);
	usleep_range(5000, 6000);
}

static void lcm_notify(int blank)
{
	struct drm_notifier_data data = {
		.id = MSM_DRM_PRIMARY_DISPLAY,
		.data = &blank,
	};

	drm_notifier_call_chain(DRM_EVENT_BLANK, &data);
}

static int lcm_prepare(struct drm_panel *panel)
{
	struct lcm *ctx = to_lcm(panel);
	int ret;

	WRITE_ONCE(esd_panel_dead, false);
	if (ctx->prepared)
		return 0;

	gpiod_set_value_cansleep(ctx->enp, 1);
	ret = lcd_bias_write_bytes(0x00, 0x14);
	if (ret < 0)
		goto power_off;
	ret = lcd_bias_write_bytes(0x01, 0x14);
	if (ret < 0)
		goto power_off;

	usleep_range(5000, 6000);
	gpiod_set_value_cansleep(ctx->reset, 0);
	usleep_range(5000, 6000);
	gpiod_set_value_cansleep(ctx->reset, 1);
	msleep(20);
	gpiod_set_value_cansleep(ctx->enn, 1);
	msleep(30);
	gpiod_set_value_cansleep(ctx->reset, 0);
	usleep_range(5000, 6000);
	gpiod_set_value_cansleep(ctx->reset, 1);
	msleep(20);
	ret = lcm_push(ctx, ctx->desc->init, ctx->desc->init_count);
	if (ret)
		goto power_off;
	ctx->prepared = true;
	lcm_notify(DRM_BLANK_UNBLANK);
	return 0;

power_off:
	gpiod_set_value_cansleep(ctx->reset, 0);
	lcm_power_off(ctx);
	return ret;
}

static int lcm_unprepare(struct drm_panel *panel)
{
	static const u8 pre_off[] = { 0x6d, 0x25, 0x00 };
	static const u8 display_off[] = { 0x28, 0x00, 0x00 };
	static const u8 sleep_in[] = { 0x10, 0x00, 0x00 };
	static const struct lcm_cmd post_sleep[] = {
		{ .len = 9, .data = { 0xbb, 0x01, 0x02, 0x03, 0x0a, 0x04, 0x13, 0x14, 0x00 } },
		{ .len = 3, .data = { 0xf1, 0x5a, 0x59 } },
		{ .len = 3, .data = { 0xf0, 0xa5, 0xa6 } },
	};
	struct lcm *ctx = to_lcm(panel);
	int ret;

	if (!ctx->prepared)
		return 0;

	lcm_notify(DRM_BLANK_POWERDOWN);
	if (!READ_ONCE(prox_lcd_reset_keep_high4))
		lcm_write(ctx, pre_off, sizeof(pre_off));

	ret = lcm_write(ctx, display_off, sizeof(display_off));
	msleep(20);
	ctx->prepared = false;

	if (READ_ONCE(prox_lcd_reset_keep_high4) && !READ_ONCE(esd_panel_dead))
		return ret;

	ret = lcm_write(ctx, sleep_in, sizeof(sleep_in));
	msleep(120);
	lcm_push(ctx, post_sleep, ARRAY_SIZE(post_sleep));

	if (READ_ONCE(lcd_reset_keep_high_4th) && !READ_ONCE(esd_panel_dead))
		return ret;

	lcm_power_off(ctx);
	return ret;
}

static int lcm_get_modes(struct drm_panel *panel, struct drm_connector *connector)
{
	struct lcm *ctx = to_lcm(panel);
	struct drm_display_mode *mode;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(ctx->desc->modes); i++) {
		mode = drm_mode_duplicate(connector->dev, &ctx->desc->modes[i]);
		if (!mode)
			return -ENOMEM;
		drm_mode_set_name(mode);
		mode->type = DRM_MODE_TYPE_DRIVER;
		if (!i)
			mode->type |= DRM_MODE_TYPE_PREFERRED;
		drm_mode_probed_add(connector, mode);
	}
	connector->display_info.width_mm = 70;
	connector->display_info.height_mm = 156;
	return ARRAY_SIZE(ctx->desc->modes);
}

static const struct drm_panel_funcs lcm_panel_funcs = {
	.prepare = lcm_prepare,
	.unprepare = lcm_unprepare,
	.get_modes = lcm_get_modes,
};

static int lcm_setbacklight_cmdq(void *dsi, dcs_write_gce cb,
			  void *handle, unsigned int level)
{
	u8 cmd[] = { 0x51, 0, 0 };
	unsigned int bl;

	if (!cb)
		return -EINVAL;

	if (level == 0)
		bl = 0;
	else if (level <= 16)
		bl = 6;
	else if (level <= 2047)
		bl = level * 77 / 100;
	else if (level <= 2549)
		bl = (level * 157000 / 503 + 937336) / 1000;
	else if (level <= 2999)
		bl = (level * 157000 / 450 + 845600) / 1000;
	else if (level <= 3899)
		bl = (level * 157000 / 900 + 1368000) / 1000;
	else if (level <= 4095)
		bl = 2047;
	else
		bl = level;

	cmd[1] = (bl >> 8) & 0xff;
	cmd[2] = bl & 0xff;
	cb(dsi, handle, cmd, sizeof(cmd));
	return 0;
}

static int lcm_ext_reset(struct drm_panel *panel, int on)
{
	gpiod_set_value_cansleep(to_lcm(panel)->reset, on);
	return 0;
}

static int lcm_ext_param_set(struct drm_panel *panel,
			      struct drm_connector *connector, unsigned int id)
{
	struct drm_display_mode *mode;
	unsigned int i = 0;

	list_for_each_entry(mode, &connector->modes, head) {
		if (i++ == id)
			return drm_mode_vrefresh(mode) == 60 ||
			       drm_mode_vrefresh(mode) == 90 ? 0 : -EINVAL;
	}
	return -EINVAL;
}

static struct mtk_panel_funcs lcm_ext_funcs = {
	.reset = lcm_ext_reset,
	.set_backlight_cmdq = lcm_setbacklight_cmdq,
	.ext_param_set = lcm_ext_param_set,
};

static const struct lcm_desc *lcm_select_panel(struct device *dev)
{
	const struct lcm_desc *desc = of_device_get_match_data(dev);
	struct device_node *chosen;
	const char *args, *name;
	size_t len;

	if (desc)
		return desc;
	chosen = of_find_node_by_path("/chosen");
	if (!chosen)
		return ERR_PTR(-ENODEV);
	if (of_property_read_string(chosen, "bootargs", &args)) {
		of_node_put(chosen);
		return ERR_PTR(-ENODEV);
	}

	name = strstr(args, "panel_name=");
	if (name && (name == args || isspace(name[-1]))) {
		name += strlen("panel_name=");
	} else {
		name = strstr(args, "lcm_name=");
		if (name && (name == args || isspace(name[-1])))
			name += strlen("lcm_name=");
		else
			name = NULL;
	}
	if (name) {
		len = strcspn(name, " \t");
		if ((strlen(lcm_desc.name) == len &&
		     !strncmp(name, lcm_desc.name, len)) ||
		    (strlen("dsi_panel_c3u_42_0f_0d_dsc_vdo") == len &&
		     !strncmp(name, "dsi_panel_c3u_42_0f_0d_dsc_vdo", len)))
			desc = &lcm_desc;
	}

	of_node_put(chosen);
	return desc ? desc : ERR_PTR(-ENODEV);
}

static int lcm_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct device_node *host, *endpoint, *remote;
	const struct lcm_desc *desc;
	struct lcm *ctx;
	int ret;

	desc = lcm_select_panel(dev);
	if (IS_ERR(desc))
		return PTR_ERR(desc);
	host = of_get_parent(dev->of_node);
	endpoint = of_graph_get_next_endpoint(host, NULL);
	remote = of_graph_get_remote_port_parent(endpoint);
	of_node_put(endpoint);
	of_node_put(host);
	if (remote != dev->of_node) {
		of_node_put(remote);
		return -ENODEV;
	}
	of_node_put(remote);
	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;
	ctx->dsi = dsi;
	ctx->desc = desc;
	mipi_dsi_set_drvdata(dsi, ctx);

	ctx->reset = devm_gpiod_get(dev, "reset", GPIOD_ASIS);
	if (IS_ERR(ctx->reset))
		return dev_err_probe(dev, PTR_ERR(ctx->reset), "reset GPIO\n");
	ctx->enp = devm_gpiod_get(dev, "enp", GPIOD_ASIS);
	if (IS_ERR(ctx->enp))
		return dev_err_probe(dev, PTR_ERR(ctx->enp), "positive bias GPIO\n");
	ctx->enn = devm_gpiod_get(dev, "enn", GPIOD_ASIS);
	if (IS_ERR(ctx->enn))
		return dev_err_probe(dev, PTR_ERR(ctx->enn), "negative bias GPIO\n");
	drm_panel_init(&ctx->panel, dev, &lcm_panel_funcs, DRM_MODE_CONNECTOR_DSI);
	ret = drm_panel_of_backlight(&ctx->panel);
	if (ret)
		return ret;

	ret = gpiod_direction_output(ctx->reset, 1);
	if (ret)
		return ret;
	ret = gpiod_direction_output(ctx->enp, 1);
	if (ret)
		return ret;
	ret = gpiod_direction_output(ctx->enn, 1);
	if (ret)
		return ret;
	ctx->prepared = true;
	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_SYNC_PULSE |
		MIPI_DSI_MODE_NO_EOT_PACKET;
	ctx->params.pll_clk = 482;
	ctx->params.data_rate = 964;
	ctx->params.phy_timcon.clk_hs_post = 36;
	ctx->params.vfp_low_power = desc->modes[0].vsync_start - 1600;
	ctx->params.dyn_fps.switch_en = 1;
	ctx->params.dyn_fps.vact_timing_fps = 90;
	ctx->params.cust_esd_check = 1;
	ctx->params.esd_check_enable = 1;
	ctx->params.lcm_esd_check_table[0].cmd = 0x0a;
	ctx->params.lcm_esd_check_table[0].count = 1;
	ctx->params.lcm_esd_check_table[0].para_list[0] = 0x9c;
	ctx->params.physical_width_um = 70310;
	ctx->params.physical_height_um = 156240;
	drm_panel_add(&ctx->panel);
	ret = mipi_dsi_attach(dsi);
	if (ret)
		goto remove_panel;
	mtk_panel_tch_handle_reg(&ctx->panel);
	ret = mtk_panel_ext_create(dev, &ctx->params,
		&lcm_ext_funcs, &ctx->panel);
	if (ret)
		goto detach;
	dev_info(dev, "%s: 720x1600, 60/90 Hz\n", desc->name);
	return 0;

detach:
	mipi_dsi_detach(dsi);
remove_panel:
	drm_panel_remove(&ctx->panel);
	return ret;
}

static void lcm_remove(struct mipi_dsi_device *dsi)
{
	struct lcm *ctx = mipi_dsi_get_drvdata(dsi);
	struct mtk_panel_ctx *ext = find_panel_ctx(&ctx->panel);

	drm_panel_disable(&ctx->panel);
	drm_panel_unprepare(&ctx->panel);
	gpiod_set_value_cansleep(ctx->reset, 0);
	lcm_power_off(ctx);
	mtk_panel_detach(ext);
	mtk_panel_remove(ext);
	mipi_dsi_detach(dsi);
	drm_panel_remove(&ctx->panel);
}

static void lcm_shutdown(struct mipi_dsi_device *dsi)
{
	struct lcm *ctx = mipi_dsi_get_drvdata(dsi);

	drm_panel_disable(&ctx->panel);
	drm_panel_unprepare(&ctx->panel);
	gpiod_set_value_cansleep(ctx->reset, 0);
	lcm_power_off(ctx);
}

static const struct of_device_id lcm_of_match[] = {
	{ .compatible = "dsi_panel_c3u,lcm" },
	{ }
};
MODULE_DEVICE_TABLE(of, lcm_of_match);

static struct mipi_dsi_driver lcm_driver = {
	.probe = lcm_probe,
	.remove = lcm_remove,
	.shutdown = lcm_shutdown,
	.driver = { .name = "dsi_panel_c3uv_42_0f_0d_vdo", .of_match_table = lcm_of_match },
};

module_mipi_dsi_driver(lcm_driver);

MODULE_DESCRIPTION("Gale C3UV 42_0f_0d VDO panel");
MODULE_LICENSE("GPL");
