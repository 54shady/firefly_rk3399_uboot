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
#include <video.h>
#include <dm/device.h>
#include <dm/uclass-internal.h>
#include <backlight.h>
#include <power/regulator.h>
#include <asm/gpio.h>

#include "rockchip_display.h"
#include "rockchip_crtc.h"
#include "rockchip_connector.h"
#include "rockchip_panel.h"
#include "rockchip_mipi_dsi.h"

#define msleep(a)	udelay(a * 1000)

struct dsi_ctrl_hdr {
	u8 dtype;	/* data type */
	u8 wait;	/* ms */
	u8 dlen;	/* payload len */
} __packed;

struct dsi_cmd_desc {
	struct dsi_ctrl_hdr dchdr;
	u8 *payload;
};

struct dsi_panel_cmds {
	u8 *buf;
	int blen;
	struct dsi_cmd_desc *cmds;
	int cmd_cnt;
};

struct rockchip_dsi_panel {
	struct udevice *dev;
	const void *blob;
	int node;

	int bus_format;

	struct udevice *power_supply;
	bool power_invert;
	struct udevice *backlight;
	struct gpio_desc enable;
	struct gpio_desc reset;

	unsigned int delay_reset;
	unsigned int delay_prepare;
	unsigned int delay_unprepare;
	unsigned int delay_enable;
	unsigned int delay_disable;
	unsigned int delay_init;

	struct dsi_panel_cmds *on_cmds;
	struct dsi_panel_cmds *off_cmds;
};

static int rockchip_dsi_panel_parse_cmds(const void *blob, int node,
					 const u8 *data, int blen,
					 struct dsi_panel_cmds *pcmds)
{
	int len;
	u8 *buf, *bp;
	struct dsi_ctrl_hdr *dchdr;
	int i, cnt;

	if (!pcmds)
		return -EINVAL;

	buf = malloc(sizeof(char) * blen);
	if (!buf)
		return -ENOMEM;

	memcpy(buf, data, blen);

	/* scan dcs commands */
	bp = buf;
	len = blen;
	cnt = 0;
	while (len > sizeof(*dchdr)) {
		dchdr = (struct dsi_ctrl_hdr *)bp;
		if (dchdr->dlen > len) {
			printf("%s: dtsi cmd=%x error, len=%d",
			       __func__, dchdr->dtype, dchdr->dlen);
			free(buf);
			return -ENOMEM;
		}

		bp += sizeof(*dchdr);
		len -= sizeof(*dchdr);
		bp += dchdr->dlen;
		len -= dchdr->dlen;
		cnt++;
	}

	if (len != 0) {
		printf("%s: dcs_cmd=%x len=%d error!", __func__, buf[0], blen);
		free(buf);
		return -ENOMEM;
	}

	pcmds->cmds = malloc(cnt * sizeof(struct dsi_cmd_desc));
	if (!pcmds->cmds) {
		free(buf);
		return -ENOMEM;
	}

	pcmds->cmd_cnt = cnt;
	pcmds->buf = buf;
	pcmds->blen = blen;

	bp = buf;
	len = blen;
	for (i = 0; i < cnt; i++) {
		dchdr = (struct dsi_ctrl_hdr *)bp;
		len -= sizeof(*dchdr);
		bp += sizeof(*dchdr);
		pcmds->cmds[i].dchdr = *dchdr;
		pcmds->cmds[i].payload = bp;
		bp += dchdr->dlen;
		len -= dchdr->dlen;
	}

	debug("%s: total_len=%d, cmd_cnt=%d\n",
	      __func__, pcmds->blen, pcmds->cmd_cnt);
	return 0;
}

static int rockchip_dsi_panel_send_cmds(struct display_state *state,
					struct dsi_panel_cmds *cmds)
{
	int i, ret;

	if (!cmds)
		return -EINVAL;

	for (i = 0; i < cmds->cmd_cnt; i++) {
		switch (cmds->cmds[i].dchdr.dtype) {
		case MIPI_DSI_GENERIC_SHORT_WRITE_0_PARAM:
		case MIPI_DSI_GENERIC_SHORT_WRITE_1_PARAM:
		case MIPI_DSI_GENERIC_SHORT_WRITE_2_PARAM:
		case MIPI_DSI_GENERIC_LONG_WRITE:
			ret = mipi_dsi_generic_write(state, cmds->cmds[i].payload,
						     cmds->cmds[i].dchdr.dlen);
			break;
		case MIPI_DSI_DCS_SHORT_WRITE:
		case MIPI_DSI_DCS_SHORT_WRITE_PARAM:
		case MIPI_DSI_DCS_LONG_WRITE:
			ret = mipi_dsi_dcs_write(state, cmds->cmds[i].payload,
						 cmds->cmds[i].dchdr.dlen);
			break;
		default:
			return -EINVAL;
		}

		if (ret)
			printf("failed to write cmd%d: %d\n", i, ret);

		if (cmds->cmds[i].dchdr.wait)
			msleep(cmds->cmds[i].dchdr.wait);
	}

	return 0;
}

static int rockchip_dsi_panel_prepare(struct display_state *state)
{
	struct panel_state *panel_state = &state->panel_state;
	struct rockchip_dsi_panel *panel = panel_state->private;
	int ret;

	if (panel->power_supply) {
		ret = regulator_set_enable(panel->power_supply,
					   panel->power_invert);
		if (ret)
			printf("%s: failed to enable power_supply",
			       __func__);
	}

	dm_gpio_set_value(&panel->enable, 1);
	msleep(panel->delay_prepare);

	dm_gpio_set_value(&panel->reset, 1);
	msleep(panel->delay_reset);
	dm_gpio_set_value(&panel->reset, 0);

	msleep(panel->delay_init);

	if (panel->on_cmds) {
		ret = rockchip_dsi_panel_send_cmds(state, panel->on_cmds);
		if (ret)
			printf("failed to send on cmds: %d\n", ret);
	}

	return 0;
}

static int rockchip_dsi_panel_unprepare(struct display_state *state)
{
	struct panel_state *panel_state = &state->panel_state;
	struct rockchip_dsi_panel *panel = panel_state->private;
	int ret;

	if (panel->off_cmds) {
		ret = rockchip_dsi_panel_send_cmds(state, panel->off_cmds);
		if (ret)
			printf("failed to send on cmds: %d\n", ret);
	}

	dm_gpio_set_value(&panel->reset, 0);

	mdelay(panel->delay_unprepare);

	dm_gpio_set_value(&panel->enable, 0);

	return 0;
}

static int rockchip_dsi_panel_enable(struct display_state *state)
{
	struct panel_state *panel_state = &state->panel_state;
	struct rockchip_dsi_panel *panel = panel_state->private;
	int ret;

	if (panel->backlight) {
		ret = backlight_enable(panel->backlight);
		mdelay(panel->delay_enable);
		return ret;
	}

	return 0;
}

static int rockchip_dsi_panel_disable(struct display_state *state)
{
	struct panel_state *panel_state = &state->panel_state;
	struct rockchip_dsi_panel *panel = panel_state->private;
	int ret;

	if (panel->backlight) {
		ret = backlight_disable(panel->backlight);
		mdelay(panel->delay_disable);
		return ret;
	}

	return 0;
}

static int rockchip_dsi_panel_parse_dt(const void *blob, int node, struct rockchip_dsi_panel *panel)
{
	const void *data;
	int len = 0;
	int ret = 0;

	panel->delay_prepare = fdtdec_get_int(blob, node, "prepare-delay-ms", 0);
	panel->delay_unprepare = fdtdec_get_int(blob, node, "unprepare-delay-ms", 0);
	panel->delay_enable = fdtdec_get_int(blob, node, "enable-delay-ms", 0);
	panel->delay_disable = fdtdec_get_int(blob, node, "disable-delay-ms", 0);
	panel->delay_init = fdtdec_get_int(blob, node, "init-delay-ms", 0);
	panel->delay_reset = fdtdec_get_int(blob, node, "reset-delay-ms", 0);
	panel->bus_format = fdtdec_get_int(blob, node, "bus-format", MEDIA_BUS_FMT_RBG888_1X24);

	data = fdt_getprop(blob, node, "panel-init-sequence", &len);
	if (data) {
		panel->on_cmds = malloc(sizeof(*panel->on_cmds));
		if (!panel->on_cmds)
			return -ENOMEM;

		ret = rockchip_dsi_panel_parse_cmds(blob, node, data, len,
						    panel->on_cmds);
		if (ret) {
			printf("failed to parse panel init sequence\n");
			goto free_on_cmds;
		}
	}

	data = fdt_getprop(blob, node, "panel-exit-sequence", &len);
	if (data) {
		panel->off_cmds = malloc(sizeof(*panel->off_cmds));
		if (!panel->off_cmds) {
			ret = -ENOMEM;
			goto free_on_cmds;
		}

		ret = rockchip_dsi_panel_parse_cmds(blob, node, data, len,
						    panel->off_cmds);
		if (ret) {
			printf("failed to parse panel exit sequence\n");
			goto free_cmds;
		}
	}

	ret = uclass_get_device_by_phandle(UCLASS_PANEL_BACKLIGHT, panel->dev,
					   "backlight", &panel->backlight);
	if (ret) {
		printf("%s: Cannot get backlight: ret=%d\n", __func__, ret);
		return ret;
	}

	ret = gpio_request_by_name(panel->dev, "enable-gpios", 0,
				   &panel->enable, GPIOD_IS_OUT);
	if (ret && ret != -ENOENT) {
		printf("%s: Warning: cannot get enable GPIO: ret=%d\n",
		      __func__, ret);
		return ret;
	}

	ret = gpio_request_by_name(panel->dev, "reset-gpios", 0,
				   &panel->reset, GPIOD_IS_OUT);
	if (ret && ret != -ENOENT) {
		printf("%s: Warning: cannot get reset GPIO: ret=%d\n",
		      __func__, ret);
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

	/* keep panel blank on init. */
	dm_gpio_set_value(&panel->enable, 0);
	dm_gpio_set_value(&panel->reset, 0);

	return 0;

free_cmds:
	free(panel->off_cmds);
free_on_cmds:
	free(panel->on_cmds);
	return ret;
}

static int rockchip_dsi_panel_init(struct display_state *state)
{
	const void *blob = state->blob;
	struct connector_state *conn_state = &state->conn_state;
	struct panel_state *panel_state = &state->panel_state;
	int node = panel_state->node;
	struct rockchip_dsi_panel *panel;
	int ret;

	panel = malloc(sizeof(*panel));
	if (!panel)
		return -ENOMEM;

	memset(panel, 0, sizeof(*panel));

	panel->blob = blob;
	panel->node = node;
	panel->dev = panel_state->dev;
	panel_state->private = panel;

	ret = rockchip_dsi_panel_parse_dt(blob, node, panel);
	if (ret) {
		printf("%s: failed to parse DT\n", __func__);
		free(panel);
		return ret;
	}

	conn_state->bus_format = panel->bus_format;

	return 0;
}

static void rockchip_dsi_panel_deinit(struct display_state *state)
{
	struct panel_state *panel_state = &state->panel_state;
	struct rockchip_dsi_panel *panel = panel_state->private;

	if (panel->on_cmds) {
		free(panel->on_cmds->buf);
		free(panel->on_cmds->cmds);
	}

	if (panel->off_cmds) {
		free(panel->off_cmds->buf);
		free(panel->off_cmds->cmds);
	}

	free(panel);
}

const struct rockchip_panel_funcs rockchip_dsi_panel_funcs = {
	.init		= rockchip_dsi_panel_init,
	.deinit		= rockchip_dsi_panel_deinit,
	.prepare	= rockchip_dsi_panel_prepare,
	.unprepare	= rockchip_dsi_panel_unprepare,
	.enable		= rockchip_dsi_panel_enable,
	.disable	= rockchip_dsi_panel_disable,
};
