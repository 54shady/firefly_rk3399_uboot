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
#include <asm/io.h>
#include <linux/list.h>
#include <linux/media-bus-format.h>
#include <clk.h>
#include <asm/arch/clock.h>
#include <linux/err.h>

#include "rockchip_display.h"
#include "rockchip_crtc.h"
#include "rockchip_connector.h"
#include "rockchip_vop.h"

static inline int us_to_vertical_line(struct drm_display_mode *mode, int us)
{
	return us * mode->clock / mode->htotal / 1000;
}

static int rockchip_vop_init_gamma(struct vop *vop, struct display_state *state)
{
	struct crtc_state *crtc_state = &state->crtc_state;
	struct connector_state *conn_state = &state->conn_state;
	u32 *lut = conn_state->gamma.lut;
	int node = crtc_state->node;
	fdt_size_t lut_size;
	int i, lut_len;
	u32 *lut_regs;

	if (!conn_state->gamma.lut)
		return 0;

	i = fdt_stringlist_search(state->blob, node, "reg-names", "gamma_lut");
	if (i < 0) {
		printf("Warning: vop not support gamma\n");
		return 0;
	}
	lut_regs = (u32 *)fdtdec_get_addr_size_auto_noparent(state->blob,
							     node, "reg", i,
							     &lut_size, false);
	if (lut_regs == (u32 *)FDT_ADDR_T_NONE) {
		printf("failed to get gamma lut register\n");
		return 0;
	}
	lut_len = lut_size / 4;
	if (lut_len != 256 && lut_len != 1024) {
		printf("Warning: unsupport gamma lut table[%d]\n", lut_len);
		return 0;
	}

	if (conn_state->gamma.size != lut_len) {
		int size = conn_state->gamma.size;
		u32 j, r, g, b, color;

		for (i = 0; i < lut_len; i++) {
			j = i * size / lut_len;
			r = lut[j] / size / size * lut_len / size;
			g = lut[j] / size % size * lut_len / size;
			b = lut[j] % size * lut_len / size;
			color = r * lut_len * lut_len + g * lut_len + b;

			writel(color, lut_regs + (i << 2));
		}
	} else {
		for (i = 0; i < lut_len; i++)
			writel(lut[i], lut_regs + (i << 2));
	}

	VOP_CTRL_SET(vop, dsp_lut_en, 1);
	VOP_CTRL_SET(vop, update_gamma_lut, 1);

	return 0;
}

static int rockchip_vop_init(struct display_state *state)
{
	struct crtc_state *crtc_state = &state->crtc_state;
	struct connector_state *conn_state = &state->conn_state;
	struct drm_display_mode *mode = &conn_state->mode;
	const struct rockchip_crtc *crtc = crtc_state->crtc;
	const struct vop_data *vop_data = crtc->data;
	struct vop *vop;
	u16 hsync_len = mode->hsync_end - mode->hsync_start;
	u16 hdisplay = mode->hdisplay;
	u16 htotal = mode->htotal;
	u16 hact_st = mode->htotal - mode->hsync_start;
	u16 hact_end = hact_st + hdisplay;
	u16 vdisplay = mode->vdisplay;
	u16 vtotal = mode->vtotal;
	u16 vsync_len = mode->vsync_end - mode->vsync_start;
	u16 vact_st = mode->vtotal - mode->vsync_start;
	u16 vact_end = vact_st + vdisplay;
	struct clk dclk, aclk;
	u32 val;
	int ret;

	vop = malloc(sizeof(*vop));
	if (!vop)
		return -ENOMEM;
	memset(vop, 0, sizeof(*vop));

	crtc_state->private = vop;
	vop->regs = (void *)fdtdec_get_addr_size_auto_noparent(state->blob,
					crtc_state->node, "reg", 0, NULL, false);
	vop->regsbak = malloc(vop_data->reg_len);
	vop->win = vop_data->win;
	vop->win_offset = vop_data->win_offset;
	vop->ctrl = vop_data->ctrl;
	vop->line_flag = vop_data->line_flag;
	vop->version = vop_data->version;

	/*
	 * TODO:
	 * Set Dclk pll parent
	 */

	ret = clk_get_by_name(crtc_state->dev, "dclk_vop", &dclk);
	if (!ret)
		ret = clk_set_rate(&dclk, mode->clock * 1000);
	if (IS_ERR_VALUE(ret)) {
		printf("%s: Failed to set dclk: ret=%d\n", __func__, ret);
		return ret;
	}

	ret = clk_get_by_name(crtc_state->dev, "aclk_vop", &aclk);
	if (!ret)
		ret = clk_set_rate(&aclk, 400 * 1000 * 1000);
	if (IS_ERR_VALUE(ret))
		printf("%s: Failed to set aclk: ret=%d\n", __func__, ret);

	memcpy(vop->regsbak, vop->regs, vop_data->reg_len);

	rockchip_vop_init_gamma(vop, state);

	VOP_CTRL_SET(vop, global_regdone_en, 1);
	VOP_CTRL_SET(vop, win_gate[0], 1);
	VOP_CTRL_SET(vop, win_gate[1], 1);
	VOP_CTRL_SET(vop, dsp_blank, 0);

	val = 0x8;
	val |= (mode->flags & DRM_MODE_FLAG_NHSYNC) ? 0 : 1;
	val |= (mode->flags & DRM_MODE_FLAG_NVSYNC) ? 0 : (1 << 1);
	VOP_CTRL_SET(vop, pin_pol, val);

	switch (conn_state->type) {
	case DRM_MODE_CONNECTOR_LVDS:
		VOP_CTRL_SET(vop, rgb_en, 1);
		VOP_CTRL_SET(vop, rgb_pin_pol, val);
		break;
	case DRM_MODE_CONNECTOR_eDP:
		VOP_CTRL_SET(vop, edp_en, 1);
		VOP_CTRL_SET(vop, edp_pin_pol, val);
		break;
	case DRM_MODE_CONNECTOR_HDMIA:
		VOP_CTRL_SET(vop, hdmi_en, 1);
		VOP_CTRL_SET(vop, hdmi_pin_pol, val);
		break;
	case DRM_MODE_CONNECTOR_DSI:
		VOP_CTRL_SET(vop, mipi_en, 1);
		VOP_CTRL_SET(vop, mipi_pin_pol, val);
		VOP_CTRL_SET(vop, mipi_dual_channel_en,
			!!(conn_state->output_type & ROCKCHIP_OUTPUT_DSI_DUAL_CHANNEL));
		VOP_CTRL_SET(vop, data01_swap,
			!!(conn_state->output_type & ROCKCHIP_OUTPUT_DSI_DUAL_LINK));
		break;
	default:
		printf("unsupport connector_type[%d]\n", conn_state->type);
	}

	if (conn_state->output_mode == ROCKCHIP_OUT_MODE_AAAA &&
	    !(vop_data->feature & VOP_FEATURE_OUTPUT_10BIT))
		conn_state->output_mode = ROCKCHIP_OUT_MODE_P888;

	switch (conn_state->bus_format) {
	case MEDIA_BUS_FMT_RGB565_1X16:
		val = DITHER_DOWN_EN(1) | DITHER_DOWN_MODE(RGB888_TO_RGB565);
		break;
	case MEDIA_BUS_FMT_RGB666_1X18:
	case MEDIA_BUS_FMT_RGB666_1X24_CPADHI:
		val = DITHER_DOWN_EN(1) | DITHER_DOWN_MODE(RGB888_TO_RGB666);
		break;
	case MEDIA_BUS_FMT_RGB888_1X24:
	default:
		val = DITHER_DOWN_EN(0) | PRE_DITHER_DOWN_EN(0);
		break;
	}
	if (conn_state->output_mode == ROCKCHIP_OUT_MODE_AAAA)
		val |= PRE_DITHER_DOWN_EN(0);
	else
		val |= PRE_DITHER_DOWN_EN(1);
	val |= DITHER_DOWN_MODE_SEL(DITHER_DOWN_ALLEGRO);
	VOP_CTRL_SET(vop, dither_down, val);

	VOP_CTRL_SET(vop, out_mode, conn_state->output_mode);
	VOP_CTRL_SET(vop, htotal_pw, (htotal << 16) | hsync_len);
	val = hact_st << 16;
	val |= hact_end;
	VOP_CTRL_SET(vop, hact_st_end, val);
	VOP_CTRL_SET(vop, hpost_st_end, val);
	VOP_CTRL_SET(vop, vtotal_pw, (vtotal << 16) | vsync_len);
	val = vact_st << 16;
	val |= vact_end;
	VOP_CTRL_SET(vop, vact_st_end, val);
	VOP_CTRL_SET(vop, vpost_st_end, val);
	VOP_CTRL_SET(vop, standby, 1);
	VOP_LINE_FLAG_SET(vop, line_flag_num[0], vact_end - 3);
	VOP_LINE_FLAG_SET(vop, line_flag_num[1],
			  vact_end - us_to_vertical_line(mode, 1000));
	vop_cfg_done(vop);

	return 0;
}

static uint16_t scl_vop_cal_scale(enum scale_mode mode, uint32_t src,
				  uint32_t dst, bool is_horizontal,
				  int vsu_mode, int *vskiplines)
{
	uint16_t val = 1 << SCL_FT_DEFAULT_FIXPOINT_SHIFT;

	if (is_horizontal) {
		if (mode == SCALE_UP)
			val = GET_SCL_FT_BIC(src, dst);
		else if (mode == SCALE_DOWN)
			val = GET_SCL_FT_BILI_DN(src, dst);
	} else {
		if (mode == SCALE_UP) {
			if (vsu_mode == SCALE_UP_BIL)
				val = GET_SCL_FT_BILI_UP(src, dst);
			else
				val = GET_SCL_FT_BIC(src, dst);
		} else if (mode == SCALE_DOWN) {
			if (vskiplines) {
				*vskiplines = scl_get_vskiplines(src, dst);
				val = scl_get_bili_dn_vskip(src, dst,
							    *vskiplines);
			} else {
				val = GET_SCL_FT_BILI_DN(src, dst);
			}
		}
	}

	return val;
}

static void scl_vop_cal_scl_fac(struct vop *vop,
				uint32_t src_w, uint32_t src_h, uint32_t dst_w,
				uint32_t dst_h, uint32_t pixel_format)
{
	uint16_t yrgb_hor_scl_mode, yrgb_ver_scl_mode;
	uint16_t cbcr_hor_scl_mode = SCALE_NONE;
	uint16_t cbcr_ver_scl_mode = SCALE_NONE;
	int hsub = drm_format_horz_chroma_subsampling(pixel_format);
	int vsub = drm_format_vert_chroma_subsampling(pixel_format);
	bool is_yuv = false;
	uint16_t cbcr_src_w = src_w / hsub;
	uint16_t cbcr_src_h = src_h / vsub;
	uint16_t vsu_mode;
	uint16_t lb_mode;
	uint32_t val;
	int vskiplines = 0;

	if (!vop->win->scl)
		return;

	if (dst_w > 3840) {
		printf("Maximum destination width (3840) exceeded\n");
		return;
	}

	if (!vop->win->scl->ext) {
		VOP_SCL_SET(vop, scale_yrgb_x,
			    scl_cal_scale2(src_w, dst_w));
		VOP_SCL_SET(vop, scale_yrgb_y,
			    scl_cal_scale2(src_h, dst_h));
		if (is_yuv) {
			VOP_SCL_SET(vop, scale_cbcr_x,
				    scl_cal_scale2(src_w, dst_w));
			VOP_SCL_SET(vop, scale_cbcr_y,
				    scl_cal_scale2(src_h, dst_h));
		}
		return;
	}

	yrgb_hor_scl_mode = scl_get_scl_mode(src_w, dst_w);
	yrgb_ver_scl_mode = scl_get_scl_mode(src_h, dst_h);

	if (is_yuv) {
		cbcr_hor_scl_mode = scl_get_scl_mode(cbcr_src_w, dst_w);
		cbcr_ver_scl_mode = scl_get_scl_mode(cbcr_src_h, dst_h);
		if (cbcr_hor_scl_mode == SCALE_DOWN)
			lb_mode = scl_vop_cal_lb_mode(dst_w, true);
		else
			lb_mode = scl_vop_cal_lb_mode(cbcr_src_w, true);
	} else {
		if (yrgb_hor_scl_mode == SCALE_DOWN)
			lb_mode = scl_vop_cal_lb_mode(dst_w, false);
		else
			lb_mode = scl_vop_cal_lb_mode(src_w, false);
	}

	VOP_SCL_SET_EXT(vop, lb_mode, lb_mode);
	if (lb_mode == LB_RGB_3840X2) {
		if (yrgb_ver_scl_mode != SCALE_NONE) {
			printf("ERROR : not allow yrgb ver scale\n");
			return;
		}
		if (cbcr_ver_scl_mode != SCALE_NONE) {
			printf("ERROR : not allow cbcr ver scale\n");
			return;
		}
		vsu_mode = SCALE_UP_BIL;
	} else if (lb_mode == LB_RGB_2560X4) {
		vsu_mode = SCALE_UP_BIL;
	} else {
		vsu_mode = SCALE_UP_BIC;
	}

	val = scl_vop_cal_scale(yrgb_hor_scl_mode, src_w, dst_w,
				true, 0, NULL);
	VOP_SCL_SET(vop, scale_yrgb_x, val);
	val = scl_vop_cal_scale(yrgb_ver_scl_mode, src_h, dst_h,
				false, vsu_mode, &vskiplines);
	VOP_SCL_SET(vop, scale_yrgb_y, val);

	VOP_SCL_SET_EXT(vop, vsd_yrgb_gt4, vskiplines == 4);
	VOP_SCL_SET_EXT(vop, vsd_yrgb_gt2, vskiplines == 2);

	VOP_SCL_SET_EXT(vop, yrgb_hor_scl_mode, yrgb_hor_scl_mode);
	VOP_SCL_SET_EXT(vop, yrgb_ver_scl_mode, yrgb_ver_scl_mode);
	VOP_SCL_SET_EXT(vop, yrgb_hsd_mode, SCALE_DOWN_BIL);
	VOP_SCL_SET_EXT(vop, yrgb_vsd_mode, SCALE_DOWN_BIL);
	VOP_SCL_SET_EXT(vop, yrgb_vsu_mode, vsu_mode);
	if (is_yuv) {
		val = scl_vop_cal_scale(cbcr_hor_scl_mode, cbcr_src_w,
					dst_w, true, 0, NULL);
		VOP_SCL_SET(vop, scale_cbcr_x, val);
		val = scl_vop_cal_scale(cbcr_ver_scl_mode, cbcr_src_h,
					dst_h, false, vsu_mode, &vskiplines);
		VOP_SCL_SET(vop, scale_cbcr_y, val);

		VOP_SCL_SET_EXT(vop, vsd_cbcr_gt4, vskiplines == 4);
		VOP_SCL_SET_EXT(vop, vsd_cbcr_gt2, vskiplines == 2);
		VOP_SCL_SET_EXT(vop, cbcr_hor_scl_mode, cbcr_hor_scl_mode);
		VOP_SCL_SET_EXT(vop, cbcr_ver_scl_mode, cbcr_ver_scl_mode);
		VOP_SCL_SET_EXT(vop, cbcr_hsd_mode, SCALE_DOWN_BIL);
		VOP_SCL_SET_EXT(vop, cbcr_vsd_mode, SCALE_DOWN_BIL);
		VOP_SCL_SET_EXT(vop, cbcr_vsu_mode, vsu_mode);
	}
}

static int rockchip_vop_set_plane(struct display_state *state)
{
	struct crtc_state *crtc_state = &state->crtc_state;
	struct connector_state *conn_state = &state->conn_state;
	struct drm_display_mode *mode = &conn_state->mode;
	u32 act_info, dsp_info, dsp_st, dsp_stx, dsp_sty;
	struct vop *vop = crtc_state->private;
	int src_w = crtc_state->src_w;
	int src_h = crtc_state->src_h;
	int crtc_x = crtc_state->crtc_x;
	int crtc_y = crtc_state->crtc_y;
	int crtc_w = crtc_state->crtc_w;
	int crtc_h = crtc_state->crtc_h;
	int xvir = crtc_state->xvir;

	act_info = (src_h - 1) << 16;
	act_info |= (src_w - 1) & 0xffff;

	dsp_info = (crtc_h - 1) << 16;
	dsp_info |= (crtc_w - 1) & 0xffff;

	dsp_stx = crtc_x + mode->htotal - mode->hsync_start;
	dsp_sty = crtc_y + mode->vtotal - mode->vsync_start;
	dsp_st = dsp_sty << 16 | (dsp_stx & 0xffff);

	if (crtc_state->ymirror)
		crtc_state->dma_addr += (src_h - 1) * xvir * 4;
	VOP_WIN_SET(vop, ymirror, crtc_state->ymirror);
	VOP_WIN_SET(vop, format, crtc_state->format);
	VOP_WIN_SET(vop, yrgb_vir, xvir);
	VOP_WIN_SET(vop, yrgb_mst, crtc_state->dma_addr);

	scl_vop_cal_scl_fac(vop, src_w, src_h, crtc_w, crtc_h,
			    crtc_state->format);

	VOP_WIN_SET(vop, act_info, act_info);
	VOP_WIN_SET(vop, dsp_info, dsp_info);
	VOP_WIN_SET(vop, dsp_st, dsp_st);
	VOP_WIN_SET(vop, rb_swap, crtc_state->rb_swap);

	VOP_WIN_SET(vop, src_alpha_ctl, 0);

	VOP_WIN_SET(vop, enable, 1);
	vop_cfg_done(vop);

	return 0;
}

static int rockchip_vop_prepare(struct display_state *state)
{
	return 0;
}

static int rockchip_vop_enable(struct display_state *state)
{
	struct crtc_state *crtc_state = &state->crtc_state;
	struct vop *vop = crtc_state->private;

	VOP_CTRL_SET(vop, standby, 0);
	vop_cfg_done(vop);

	return 0;
}

static int rockchip_vop_disable(struct display_state *state)
{
	struct crtc_state *crtc_state = &state->crtc_state;
	struct vop *vop = crtc_state->private;

	VOP_CTRL_SET(vop, standby, 1);
	vop_cfg_done(vop);
	return 0;
}

static int rockchip_vop_fixup_dts(struct display_state *state, void *blob)
{
	struct crtc_state *crtc_state = &state->crtc_state;
	struct panel_state *pstate = &state->panel_state;
	uint32_t phandle;
	char path[100];
	int ret, dsp_lut_node;

	if (!pstate->dsp_lut_node)
		return 0;

	ret = fdt_get_path(state->blob, pstate->dsp_lut_node, path, sizeof(path));
	if (ret < 0) {
		printf("failed to get dsp_lut path[%s], ret=%d\n",
			path, ret);
		return ret;
	}

	dsp_lut_node = fdt_path_offset(blob, path);
	phandle = fdt_get_phandle(blob, dsp_lut_node);
	if (!phandle) {
		phandle = fdt_alloc_phandle(blob);
		if (!phandle) {
			printf("failed to alloc phandle\n");
			return -ENOMEM;
		}

		fdt_set_phandle(blob, dsp_lut_node, phandle);
	}

	ret = fdt_get_path(state->blob, crtc_state->node, path, sizeof(path));
	if (ret < 0) {
		printf("failed to get route path[%s], ret=%d\n",
			path, ret);
		return ret;
	}

	do_fixup_by_path_u32(blob, path, "dsp-lut", phandle, 1);

	return 0;
}

const struct rockchip_crtc_funcs rockchip_vop_funcs = {
	.init = rockchip_vop_init,
	.set_plane = rockchip_vop_set_plane,
	.prepare = rockchip_vop_prepare,
	.enable = rockchip_vop_enable,
	.disable = rockchip_vop_disable,
	.fixup_dts = rockchip_vop_fixup_dts,
};
