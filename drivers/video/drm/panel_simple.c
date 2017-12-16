/*
 * (C) Copyright 2008-2017 Fuzhou Rockchip Electronics Co., Ltd
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

#include <config.h>
#include <common.h>
#include <errno.h>
#include <malloc.h>
#include <fdtdec.h>
#include <fdt_support.h>
#include <asm/unaligned.h>
#include <linux/list.h>
#include <linux/media-bus-format.h>
#include <dm/uclass.h>
#include <dm/uclass-id.h>
#include <asm/gpio.h>
#include <backlight.h>
#include <power/regulator.h>

#include "rockchip_display.h"
#include "rockchip_crtc.h"
#include "rockchip_connector.h"
#include "rockchip_panel.h"

#define msleep(a)	udelay(a * 1000)

struct panel_simple {
	struct udevice *dev;
	const void *blob;
	int node;

	const struct drm_display_mode *mode;
	int bus_format;

	struct udevice *power_supply;
	bool power_invert;
	struct udevice *backlight;
	struct gpio_desc enable;

	int delay_prepare;
	int delay_unprepare;
	int delay_enable;
	int delay_disable;
};

static int panel_simple_prepare(struct display_state *state)
{
	struct panel_state *panel_state = &state->panel_state;
	struct panel_simple *panel = panel_state->private;
	int ret;

	if (panel->power_supply) {
		ret = regulator_set_enable(panel->power_supply,
					   panel->power_invert);
		if (ret)
			printf("%s: failed to enable power_supply",
			       __func__);
	}

	dm_gpio_set_value(&panel->enable, 1);
	mdelay(panel->delay_prepare);

	return 0;
}

static int panel_simple_unprepare(struct display_state *state)
{
	struct panel_state *panel_state = &state->panel_state;
	struct panel_simple *panel = panel_state->private;

	dm_gpio_set_value(&panel->enable, 0);
	mdelay(panel->delay_unprepare);

	return 0;
}

static int panel_simple_enable(struct display_state *state)
{
	struct panel_state *panel_state = &state->panel_state;
	struct panel_simple *panel = panel_state->private;
	int ret;

	if (panel->backlight) {
		ret = backlight_enable(panel->backlight);
		mdelay(panel->delay_enable);
		return ret;
	}

	return 0;
}

static int panel_simple_disable(struct display_state *state)
{
	struct panel_state *panel_state = &state->panel_state;
	struct panel_simple *panel = panel_state->private;
	int ret;

	if (panel->backlight) {
		ret = backlight_disable(panel->backlight);
		mdelay(panel->delay_disable);
		return ret;
	}

	return 0;
}

static int panel_simple_parse_dt(const void *blob, int node,
				 struct panel_simple *panel)
{
	int ret;

	ret = gpio_request_by_name(panel->dev, "enable-gpios", 0,
				   &panel->enable, GPIOD_IS_OUT);
	if (ret && ret != -ENOENT) {
		printf("%s: Warning: cannot get enable GPIO: ret=%d\n",
		      __func__, ret);
		return ret;
	}

	ret = uclass_get_device_by_phandle(UCLASS_PANEL_BACKLIGHT, panel->dev,
					   "backlight", &panel->backlight);
	if (ret && ret != -ENOENT) {
		printf("%s: Cannot get backlight: ret=%d\n", __func__, ret);
		return ret;
	}

	ret = uclass_get_device_by_phandle(UCLASS_REGULATOR, panel->dev,
					   "power-supply",
					   &panel->power_supply);
	if (ret && ret != -ENOENT) {
		printf("%s: Cannot get power supply: ret=%d\n", __func__, ret);
		return ret;
	}

	panel->power_invert = !!fdtdec_get_int(blob, node, "power_invert", 0);

	panel->delay_prepare = fdtdec_get_int(blob, node, "delay,prepare", 0);
	panel->delay_unprepare = fdtdec_get_int(blob, node, "delay,unprepare", 0);
	panel->delay_enable = fdtdec_get_int(blob, node, "delay,enable", 0);
	panel->delay_disable = fdtdec_get_int(blob, node, "delay,disable", 0);
	panel->bus_format = fdtdec_get_int(blob, node, "bus-format", MEDIA_BUS_FMT_RBG888_1X24);

	printf("delay prepare[%d] unprepare[%d] enable[%d] disable[%d]\n",
	       panel->delay_prepare, panel->delay_unprepare,
	       panel->delay_enable, panel->delay_disable);

	/* keep panel blank on init. */
	dm_gpio_set_value(&panel->enable, 0);

	return 0;
}

static int panel_simple_init(struct display_state *state)
{
	const void *blob = state->blob;
	struct connector_state *conn_state = &state->conn_state;
	struct panel_state *panel_state = &state->panel_state;
	int node = panel_state->node;
	const struct drm_display_mode *mode = panel_state->panel->data;
	struct panel_simple *panel;
	int ret;

	panel = malloc(sizeof(*panel));
	if (!panel)
		return -ENOMEM;

	memset(panel, 0, sizeof(*panel));
	panel->blob = blob;
	panel->node = node;
	panel->mode = mode;
	panel->dev = panel_state->dev;
	panel_state->private = panel;

	ret = panel_simple_parse_dt(blob, node, panel);
	if (ret) {
		printf("%s: failed to parse DT\n", __func__);
		free(panel);
		return ret;
	}

	conn_state->bus_format = panel->bus_format;

	return 0;
}

static void panel_simple_deinit(struct display_state *state)
{
	struct panel_state *panel_state = &state->panel_state;
	struct panel_simple *panel = panel_state->private;

	free(panel);
}

const struct rockchip_panel_funcs panel_simple_funcs = {
	.init		= panel_simple_init,
	.deinit		= panel_simple_deinit,
	.prepare	= panel_simple_prepare,
	.unprepare	= panel_simple_unprepare,
	.enable		= panel_simple_enable,
	.disable	= panel_simple_disable,
};
