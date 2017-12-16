/*
 * (C) Copyright 2008-2017 Fuzhou Rockchip Electronics Co., Ltd
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

#include <asm/unaligned.h>
#include <config.h>
#include <common.h>
#include <errno.h>
#include <libfdt.h>
#include <fdtdec.h>
#include <fdt_support.h>
#include <linux/list.h>
#include <linux/compat.h>
#include <linux/media-bus-format.h>
#include <malloc.h>
#include <video.h>
#include <video_rockchip.h>
#include <dm/device.h>
#include <dm/uclass-internal.h>
#include <asm/arch-rockchip/resource_img.h>

#include "bmp_helper.h"
#include "rockchip_display.h"
#include "rockchip_crtc.h"
#include "rockchip_connector.h"
#include "rockchip_phy.h"
#include "rockchip_panel.h"

#define RK_BLK_SIZE 512

DECLARE_GLOBAL_DATA_PTR;
static LIST_HEAD(rockchip_display_list);
static LIST_HEAD(logo_cache_list);

#ifdef CONFIG_DRM_ROCKCHIP_VIDEO_FRAMEBUFFER
 #define DRM_ROCKCHIP_FB_WIDTH		1920
 #define DRM_ROCKCHIP_FB_HEIGHT		1080
 #define DRM_ROCKCHIP_FB_BPP		VIDEO_BPP32
#else
 #define DRM_ROCKCHIP_FB_WIDTH		0
 #define DRM_ROCKCHIP_FB_HEIGHT		0
 #define DRM_ROCKCHIP_FB_BPP		VIDEO_BPP32
#endif

#define MEMORY_POOL_SIZE	32 * 1024 * 1024
#define DRM_ROCKCHIP_FB_SIZE \
	VNBYTES(DRM_ROCKCHIP_FB_BPP) * DRM_ROCKCHIP_FB_WIDTH * DRM_ROCKCHIP_FB_HEIGHT

static unsigned long memory_start;
static unsigned long memory_end;

static void init_display_buffer(ulong base)
{
	memory_start = base + DRM_ROCKCHIP_FB_SIZE;
	memory_end = memory_start;
}

static void *get_display_buffer(int size)
{
	unsigned long roundup_memory = roundup(memory_end, PAGE_SIZE);
	void *buf;

	if (roundup_memory + size > memory_start + MEMORY_POOL_SIZE) {
		printf("failed to alloc %dbyte memory to display\n", size);
		return NULL;
	}
	buf = (void *)roundup_memory;

	memory_end = roundup_memory + size;

	return buf;
}

#if 0
static unsigned long get_display_size(void)
{
	return memory_end - memory_start;
}
#endif

static bool can_direct_logo(int bpp)
{
	return bpp == 24 || bpp == 32;
}

static struct udevice *find_panel_device_by_node(const void *blob,
						 int panel_node)
{
	struct udevice *dev;
	int ret;

	ret = uclass_find_device_by_of_offset(UCLASS_PANEL, panel_node, &dev);
	if (ret) {
		printf("Warn: %s: can't find panel driver\n",
		       fdt_get_name(blob, panel_node, NULL));
		return NULL;
	}

	return dev;
}

static struct udevice *get_panel_device(struct display_state *state, int conn_node)
{
	struct panel_state *panel_state = &state->panel_state;
	const void *blob = state->blob;
	int panel, ports, port, ep, remote, ph, nodedepth;
	struct udevice *dev;

	panel = fdt_subnode_offset(blob, conn_node, "panel");
	if (panel > 0 && fdt_device_is_available(blob, panel)) {
		dev = find_panel_device_by_node(blob, panel);
		if (dev) {
			panel_state->node = panel;
			return dev;
		}
	}

	ports = fdt_subnode_offset(blob, conn_node, "ports");
	if (ports < 0)
		return NULL;

	fdt_for_each_subnode(port, blob, ports) {
		fdt_for_each_subnode(ep, blob, port) {
			ph = fdt_getprop_u32_default_node(blob, ep, 0,
							  "remote-endpoint", 0);
			if (!ph)
				continue;

			remote = fdt_node_offset_by_phandle(blob, ph);

			nodedepth = fdt_node_depth(blob, remote);
			if (nodedepth < 2)
				continue;

			panel = fdt_supernode_atdepth_offset(blob, remote,
							     nodedepth - 2,
							     NULL);
			if (!fdt_device_is_available(blob, panel)) {
				debug("[%s]: panel is disabled\n",
				      fdt_get_name(blob, panel, NULL));
				continue;
			}
			dev = find_panel_device_by_node(blob, panel);
			if (dev) {
				panel_state->node = panel;
				return dev;
			}
		}
	}

	return NULL;
}

static int connector_phy_init(struct display_state *state)
{
	struct connector_state *conn_state = &state->conn_state;
	int conn_node = conn_state->node;
	const void *blob = state->blob;
	const struct rockchip_phy *phy;
	int phy_node, phandle;
	struct udevice *dev;
	int ret;

	phandle = fdt_getprop_u32_default_node(blob, conn_node, 0,
					       "phys", -1);
	if (phandle < 0)
		return 0;

	phy_node = fdt_node_offset_by_phandle(blob, phandle);
	if (phy_node < 0) {
		printf("failed to find phy node\n");
		return phy_node;
	}

	ret = uclass_find_device_by_of_offset(UCLASS_PHY, phy_node, &dev);
	if (ret) {
		printf("Warn: %s: can't find phy driver\n",
		       fdt_get_name(blob, phy_node, NULL));
		return ret;
	}
	phy = (const struct rockchip_phy *)dev_get_driver_data(dev);
	if (!phy) {
		printf("failed to find phy driver\n");
		return 0;
	}

	conn_state->phy_dev = dev;
	conn_state->phy_node = phy_node;

	if (!phy->funcs || !phy->funcs->init ||
	    phy->funcs->init(state)) {
		printf("failed to init phy driver\n");
		return -EINVAL;
	}

	conn_state->phy = phy;
	return 0;
}

static int connector_panel_init(struct display_state *state)
{
	struct connector_state *conn_state = &state->conn_state;
	struct panel_state *panel_state = &state->panel_state;
	struct udevice *dev;
	const void *blob = state->blob;
	int conn_node = conn_state->node;
	const struct rockchip_panel *panel;
	int dsp_lut_node;
	int ret, len;

	dm_scan_fdt_dev(conn_state->dev);

	dev = get_panel_device(state, conn_node);
	if (!dev) {
		return 0;
	}

	panel = (const struct rockchip_panel *)dev_get_driver_data(dev);
	if (!panel) {
		printf("failed to find panel driver\n");
		return 0;
	}

	panel_state->dev = dev;
	panel_state->panel = panel;

	ret = rockchip_panel_init(state);
	if (ret) {
		printf("failed to init panel driver\n");
		return ret;
	}

	dsp_lut_node = fdt_subnode_offset(blob, panel_state->node, "dsp-lut");
	fdt_getprop(blob, dsp_lut_node, "gamma-lut", &len);
	if (len > 0) {
		conn_state->gamma.size  = len / sizeof(u32);
		conn_state->gamma.lut = malloc(len);
		if (!conn_state->gamma.lut) {
			printf("malloc gamma lut failed\n");
			return -ENOMEM;
		}
		if (fdtdec_get_int_array(blob, dsp_lut_node, "gamma-lut",
					 conn_state->gamma.lut,
					 conn_state->gamma.size)) {
			printf("Cannot decode gamma_lut\n");
			conn_state->gamma.lut = NULL;
			return -EINVAL;
		}
		panel_state->dsp_lut_node = dsp_lut_node;
	}

	return 0;
}

int drm_mode_vrefresh(const struct drm_display_mode *mode)
{
	int refresh = 0;
	unsigned int calc_val;

	if (mode->vrefresh > 0) {
		refresh = mode->vrefresh;
	} else if (mode->htotal > 0 && mode->vtotal > 0) {
		int vtotal;

		vtotal = mode->vtotal;
		/* work out vrefresh the value will be x1000 */
		calc_val = (mode->clock * 1000);
		calc_val /= mode->htotal;
		refresh = (calc_val + vtotal / 2) / vtotal;

		if (mode->flags & DRM_MODE_FLAG_INTERLACE)
			refresh *= 2;
		if (mode->flags & DRM_MODE_FLAG_DBLSCAN)
			refresh /= 2;
		if (mode->vscan > 1)
			refresh /= mode->vscan;
	}
	return refresh;
}

static int display_get_timing_from_dts(int panel, const void *blob,
				       struct drm_display_mode *mode)
{
	int timing, phandle, native_mode;
	int hactive, vactive, pixelclock;
	int hfront_porch, hback_porch, hsync_len;
	int vfront_porch, vback_porch, vsync_len;
	int val, flags = 0;

	timing = fdt_subnode_offset(blob, panel, "display-timings");
	if (timing < 0)
		return -ENODEV;

	native_mode = fdt_subnode_offset(blob, timing, "timing");
	if (native_mode < 0) {
		phandle = fdt_getprop_u32_default_node(blob, timing, 0,
						       "native-mode", -1);
		native_mode = fdt_node_offset_by_phandle_node(blob, timing, phandle);
		if (native_mode <= 0) {
			printf("failed to get display timings from DT\n");
			return -ENXIO;
		}
	}

#define FDT_GET_INT(val, name) \
	val = fdtdec_get_int(blob, native_mode, name, -1); \
	if (val < 0) { \
		printf("Can't get %s\n", name); \
		return -ENXIO; \
	}

	FDT_GET_INT(hactive, "hactive");
	FDT_GET_INT(vactive, "vactive");
	FDT_GET_INT(pixelclock, "clock-frequency");
	FDT_GET_INT(hsync_len, "hsync-len");
	FDT_GET_INT(hfront_porch, "hfront-porch");
	FDT_GET_INT(hback_porch, "hback-porch");
	FDT_GET_INT(vsync_len, "vsync-len");
	FDT_GET_INT(vfront_porch, "vfront-porch");
	FDT_GET_INT(vback_porch, "vback-porch");
	FDT_GET_INT(val, "hsync-active");
	flags |= val ? DRM_MODE_FLAG_PHSYNC : DRM_MODE_FLAG_NHSYNC;
	FDT_GET_INT(val, "vsync-active");
	flags |= val ? DRM_MODE_FLAG_PVSYNC : DRM_MODE_FLAG_NVSYNC;

	mode->hdisplay = hactive;
	mode->hsync_start = mode->hdisplay + hfront_porch;
	mode->hsync_end = mode->hsync_start + hsync_len;
	mode->htotal = mode->hsync_end + hback_porch;

	mode->vdisplay = vactive;
	mode->vsync_start = mode->vdisplay + vfront_porch;
	mode->vsync_end = mode->vsync_start + vsync_len;
	mode->vtotal = mode->vsync_end + vback_porch;

	mode->clock = pixelclock / 1000;
	mode->flags = flags;

	return 0;
}

static int display_get_timing(struct display_state *state)
{
	struct connector_state *conn_state = &state->conn_state;
	const struct rockchip_connector *conn = conn_state->connector;
	const struct rockchip_connector_funcs *conn_funcs = conn->funcs;
	struct drm_display_mode *mode = &conn_state->mode;
	const struct drm_display_mode *m;
	const void *blob = state->blob;
	struct panel_state *panel_state = &state->panel_state;
	int panel = panel_state->node;

	if (panel > 0 && !display_get_timing_from_dts(panel, blob, mode)) {
		printf("Using display timing dts\n");
		goto done;
	}

	m = rockchip_get_display_mode_from_panel(state);
	if (m) {
		printf("Using display timing from compatible panel driver\n");
		memcpy(mode, m, sizeof(*m));
		goto done;
	}

	rockchip_panel_prepare(state);

	if (conn_funcs->get_edid && !conn_funcs->get_edid(state)) {
		int panel_bits_per_colourp;

		if (!edid_get_drm_mode((void *)&conn_state->edid,
				     sizeof(conn_state->edid), mode,
				     &panel_bits_per_colourp)) {
			printf("Using display timing from edid\n");
			edid_print_info((void *)&conn_state->edid);
			goto done;
		}
	}

	printf("failed to find display timing\n");
	return -ENODEV;
done:
	printf("Detailed mode clock %u kHz, flags[%x]\n"
	       "    H: %04d %04d %04d %04d\n"
	       "    V: %04d %04d %04d %04d\n"
	       "bus_format: %x\n",
	       mode->clock, mode->flags,
	       mode->hdisplay, mode->hsync_start,
	       mode->hsync_end, mode->htotal,
	       mode->vdisplay, mode->vsync_start,
	       mode->vsync_end, mode->vtotal,
	       conn_state->bus_format);

	return 0;
}

static int display_init(struct display_state *state)
{
	struct connector_state *conn_state = &state->conn_state;
	const struct rockchip_connector *conn = conn_state->connector;
	const struct rockchip_connector_funcs *conn_funcs = conn->funcs;
	struct crtc_state *crtc_state = &state->crtc_state;
	const struct rockchip_crtc *crtc = crtc_state->crtc;
	const struct rockchip_crtc_funcs *crtc_funcs = crtc->funcs;
	int ret = 0;

	if (state->is_init)
		return 0;

	if (!conn_funcs || !crtc_funcs) {
		printf("failed to find connector or crtc functions\n");
		return -ENXIO;
	}

	if (conn_funcs->init) {
		ret = conn_funcs->init(state);
		if (ret)
			goto deinit;
	}
	/*
	 * support hotplug, but not connect;
	 */
	if (conn_funcs->detect) {
		ret = conn_funcs->detect(state);
		if (!ret)
			goto deinit;
	}

	if (conn_funcs->get_timing) {
		ret = conn_funcs->get_timing(state);
		if (ret)
			goto deinit;
	} else {
		ret = display_get_timing(state);
		if (ret)
			goto deinit;
	}

	if (crtc_funcs->init) {
		ret = crtc_funcs->init(state);
		if (ret)
			goto deinit;
	}

	state->is_init = 1;

	return 0;

deinit:
	if (conn_funcs->deinit)
		conn_funcs->deinit(state);
	return ret;
}

static int display_set_plane(struct display_state *state)
{
	struct crtc_state *crtc_state = &state->crtc_state;
	const struct rockchip_crtc *crtc = crtc_state->crtc;
	const struct rockchip_crtc_funcs *crtc_funcs = crtc->funcs;
	int ret;

	if (!state->is_init)
		return -EINVAL;

	if (crtc_funcs->set_plane) {
		ret = crtc_funcs->set_plane(state);
		if (ret)
			return ret;
	}

	return 0;
}

static int display_enable(struct display_state *state)
{
	struct connector_state *conn_state = &state->conn_state;
	const struct rockchip_connector *conn = conn_state->connector;
	const struct rockchip_connector_funcs *conn_funcs = conn->funcs;
	struct crtc_state *crtc_state = &state->crtc_state;
	const struct rockchip_crtc *crtc = crtc_state->crtc;
	const struct rockchip_crtc_funcs *crtc_funcs = crtc->funcs;
	int ret = 0;

	display_init(state);

	if (!state->is_init)
		return -EINVAL;

	if (state->is_enable)
		return 0;

	if (crtc_funcs->prepare) {
		ret = crtc_funcs->prepare(state);
		if (ret)
			return ret;
	}

	if (conn_funcs->prepare) {
		ret = conn_funcs->prepare(state);
		if (ret)
			goto unprepare_crtc;
	}

	rockchip_panel_prepare(state);

	if (crtc_funcs->enable) {
		ret = crtc_funcs->enable(state);
		if (ret)
			goto unprepare_conn;
	}

	if (conn_funcs->enable) {
		ret = conn_funcs->enable(state);
		if (ret)
			goto disable_crtc;
	}

	rockchip_panel_enable(state);

	state->is_enable = true;

	return 0;
unprepare_crtc:
	if (crtc_funcs->unprepare)
		crtc_funcs->unprepare(state);
unprepare_conn:
	if (conn_funcs->unprepare)
		conn_funcs->unprepare(state);
disable_crtc:
	if (crtc_funcs->disable)
		crtc_funcs->disable(state);
	return ret;
}

static int display_disable(struct display_state *state)
{
	struct connector_state *conn_state = &state->conn_state;
	const struct rockchip_connector *conn = conn_state->connector;
	const struct rockchip_connector_funcs *conn_funcs = conn->funcs;
	struct crtc_state *crtc_state = &state->crtc_state;
	const struct rockchip_crtc *crtc = crtc_state->crtc;
	const struct rockchip_crtc_funcs *crtc_funcs = crtc->funcs;

	if (!state->is_init)
		return 0;

	if (!state->is_enable)
		return 0;

	rockchip_panel_disable(state);

	if (crtc_funcs->disable)
		crtc_funcs->disable(state);

	if (conn_funcs->disable)
		conn_funcs->disable(state);

	rockchip_panel_unprepare(state);

	if (conn_funcs->unprepare)
		conn_funcs->unprepare(state);

	state->is_enable = 0;
	state->is_init = 0;

	return 0;
}

static int display_logo(struct display_state *state)
{
	struct crtc_state *crtc_state = &state->crtc_state;
	struct connector_state *conn_state = &state->conn_state;
	struct logo_info *logo = &state->logo;
	int hdisplay, vdisplay;

	display_init(state);
	if (!state->is_init)
		return -ENODEV;

	switch (logo->bpp) {
	case 16:
		crtc_state->format = ROCKCHIP_FMT_RGB565;
		break;
	case 24:
		crtc_state->format = ROCKCHIP_FMT_RGB888;
		break;
	case 32:
		crtc_state->format = ROCKCHIP_FMT_ARGB8888;
		break;
	default:
		printf("can't support bmp bits[%d]\n", logo->bpp);
		return -EINVAL;
	}
	crtc_state->rb_swap = logo->bpp != 32;
	hdisplay = conn_state->mode.hdisplay;
	vdisplay = conn_state->mode.vdisplay;
	crtc_state->src_w = logo->width;
	crtc_state->src_h = logo->height;
	crtc_state->src_x = 0;
	crtc_state->src_y = 0;
	crtc_state->ymirror = logo->ymirror;

	crtc_state->dma_addr = (u32)(unsigned long)logo->mem + logo->offset;
	crtc_state->xvir = ALIGN(crtc_state->src_w * logo->bpp, 32) >> 5;

	if (logo->mode == ROCKCHIP_DISPLAY_FULLSCREEN) {
		crtc_state->crtc_x = 0;
		crtc_state->crtc_y = 0;
		crtc_state->crtc_w = hdisplay;
		crtc_state->crtc_h = vdisplay;
	} else {
		if (crtc_state->src_w >= hdisplay) {
			crtc_state->crtc_x = 0;
			crtc_state->crtc_w = hdisplay;
		} else {
			crtc_state->crtc_x = (hdisplay - crtc_state->src_w) / 2;
			crtc_state->crtc_w = crtc_state->src_w;
		}

		if (crtc_state->src_h >= vdisplay) {
			crtc_state->crtc_y = 0;
			crtc_state->crtc_h = vdisplay;
		} else {
			crtc_state->crtc_y = (vdisplay - crtc_state->src_h) / 2;
			crtc_state->crtc_h = crtc_state->src_h;
		}
	}

	display_set_plane(state);
	display_enable(state);

	return 0;
}

static int get_crtc_id(const void *blob, int connect)
{
	int phandle, remote;
	int val;

	phandle = fdt_getprop_u32_default_node(blob, connect, 0,
					       "remote-endpoint", -1);
	if (phandle < 0)
		goto err;
	remote = fdt_node_offset_by_phandle(blob, phandle);

	val = fdtdec_get_int(blob, remote, "reg", -1);
	if (val < 0)
		goto err;

	return val;
err:
	printf("Can't get crtc id, default set to id = 0\n");
	return 0;
}

static int find_crtc_node(const void *blob, int node)
{
	int nodedepth = fdt_node_depth(blob, node);

	if (nodedepth < 2)
		return -EINVAL;

	return fdt_supernode_atdepth_offset(blob, node,
					    nodedepth - 2, NULL);
}

static int find_connector_node(const void *blob, int node)
{
	int phandle, remote;
	int nodedepth;

	phandle = fdt_getprop_u32_default_node(blob, node, 0,
					       "remote-endpoint", -1);
	remote = fdt_node_offset_by_phandle(blob, phandle);
	nodedepth = fdt_node_depth(blob, remote);

	return fdt_supernode_atdepth_offset(blob, remote,
					    nodedepth - 3, NULL);
}

struct rockchip_logo_cache *find_or_alloc_logo_cache(const char *bmp)
{
	struct rockchip_logo_cache *tmp, *logo_cache = NULL;

	list_for_each_entry(tmp, &logo_cache_list, head) {
		if (!strcmp(tmp->name, bmp)) {
			logo_cache = tmp;
			break;
		}
	}

	if (!logo_cache) {
		logo_cache = malloc(sizeof(*logo_cache));
		if (!logo_cache) {
			printf("failed to alloc memory for logo cache\n");
			return NULL;
		}
		memset(logo_cache, 0, sizeof(*logo_cache));
		strcpy(logo_cache->name, bmp);
		INIT_LIST_HEAD(&logo_cache->head);
		list_add_tail(&logo_cache->head, &logo_cache_list);
	}

	return logo_cache;
}

static int load_bmp_logo(struct logo_info *logo, const char *bmp_name)
{
#ifdef CONFIG_ROCKCHIP_RESOURCE_IMAGE
	struct rockchip_logo_cache *logo_cache;
	struct bmp_header *header;
	void *dst = NULL, *pdst;
	int size, len;
	int ret = 0;

	if (!logo || !bmp_name)
		return -EINVAL;
	logo_cache = find_or_alloc_logo_cache(bmp_name);
	if (!logo_cache)
		return -ENOMEM;

	if (logo_cache->logo.mem) {
		memcpy(logo, &logo_cache->logo, sizeof(*logo));
		return 0;
	}

	header = malloc(RK_BLK_SIZE);
	if (!header)
		return -ENOMEM;

	len = rockchip_read_resource_file(header, bmp_name, 0, RK_BLK_SIZE);
	if (len != RK_BLK_SIZE) {
		ret = -EINVAL;
		goto free_header;
	}

	logo->bpp = get_unaligned_le16(&header->bit_count);
	logo->width = get_unaligned_le32(&header->width);
	logo->height = get_unaligned_le32(&header->height);
	size = get_unaligned_le32(&header->file_size);
	if (!can_direct_logo(logo->bpp)) {
		if (size > MEMORY_POOL_SIZE) {
			printf("failed to use boot buf as temp bmp buffer\n");
			ret = -ENOMEM;
			goto free_header;
		}
		pdst = get_display_buffer(size);

	} else {
		pdst = get_display_buffer(size);
		dst = pdst;
	}

	len = rockchip_read_resource_file(pdst, bmp_name, 0, size);
	if (len != size) {
		printf("failed to load bmp %s\n", bmp_name);
		ret = -ENOENT;
		goto free_header;
	}

	if (!can_direct_logo(logo->bpp)) {
		int dst_size;
		/*
		 * TODO: force use 16bpp if bpp less than 16;
		 */
		logo->bpp = (logo->bpp <= 16) ? 16 : logo->bpp;
		dst_size = logo->width * logo->height * logo->bpp >> 3;

		dst = get_display_buffer(dst_size);
		if (!dst) {
			ret = -ENOMEM;
			goto free_header;
		}
		if (bmpdecoder(pdst, dst, logo->bpp)) {
			printf("failed to decode bmp %s\n", bmp_name);
			ret = -EINVAL;
			goto free_header;
		}
		flush_dcache_range((ulong)dst,
				   ALIGN((ulong)dst + dst_size,
					 CONFIG_SYS_CACHELINE_SIZE));

		logo->offset = 0;
		logo->ymirror = 0;
	} else {
		logo->offset = get_unaligned_le32(&header->data_offset);
		logo->ymirror = 1;
	}
	logo->mem = dst;

	memcpy(&logo_cache->logo, logo, sizeof(*logo));

free_header:

	free(header);

	return ret;
#else
	return -EINVAL;
#endif
}

void rockchip_show_fbbase(ulong fbbase)
{
	struct display_state *s;

	list_for_each_entry(s, &rockchip_display_list, head) {
		s->logo.mode = ROCKCHIP_DISPLAY_FULLSCREEN;
		s->logo.mem = (char *)fbbase;
		s->logo.width = DRM_ROCKCHIP_FB_WIDTH;
		s->logo.height = DRM_ROCKCHIP_FB_HEIGHT;
		s->logo.bpp = 32;
		s->logo.ymirror = 0;

		display_logo(s);
	}
}

void rockchip_show_bmp(const char *bmp)
{
	struct display_state *s;

	if (!bmp) {
		list_for_each_entry(s, &rockchip_display_list, head)
			display_disable(s);
		return;
	}

	list_for_each_entry(s, &rockchip_display_list, head) {
		s->logo.mode = s->charge_logo_mode;
		if (load_bmp_logo(&s->logo, bmp))
			continue;
		display_logo(s);
	}
}

void rockchip_show_logo(void)
{
	struct display_state *s;

	list_for_each_entry(s, &rockchip_display_list, head) {
		s->logo.mode = s->logo_mode;
		if (load_bmp_logo(&s->logo, s->ulogo_name))
			printf("failed to display uboot logo\n");
		else
			display_logo(s);
		if (load_bmp_logo(&s->logo, s->klogo_name))
			printf("failed to display kernel logo\n");
	}
}

static int rockchip_display_probe(struct udevice *dev)
{
	struct video_priv *uc_priv = dev_get_uclass_priv(dev);
	struct video_uc_platdata *plat = dev_get_uclass_platdata(dev);
	const void *blob = gd->fdt_blob;
	int route, child, phandle, connect, crtc_node, conn_node;
	struct udevice *crtc_dev, *conn_dev;
	const struct rockchip_crtc *crtc;
	const struct rockchip_connector *conn;
	struct display_state *s;
	const char *name;
	int ret;

	/* Before relocation we don't need to do anything */
	if (!(gd->flags & GD_FLG_RELOC))
		return 0;

	route = fdt_path_offset(blob, "/display-subsystem/route");
	if (route < 0) {
		printf("Can't find display display route node\n");
		return -ENODEV;
	}

	if (!fdt_device_is_available(blob, route))
		return -ENODEV;

	init_display_buffer(plat->base);

	fdt_for_each_subnode(child, blob, route) {
		if (!fdt_device_is_available(blob, child))
			continue;

		phandle = fdt_getprop_u32_default_node(blob, child, 0,
						       "connect", -1);
		if (phandle < 0) {
			printf("Warn: %s: can't find connect node's handle\n",
			       fdt_get_name(blob, child, NULL));
			continue;
		}

		connect = fdt_node_offset_by_phandle(blob, phandle);
		if (connect < 0) {
			printf("Warn: %s: can't find connect node\n",
			       fdt_get_name(blob, child, NULL));
			continue;
		}

		crtc_node = find_crtc_node(blob, connect);
		if (!fdt_device_is_available(blob, crtc_node)) {
			printf("Warn: %s: crtc node is not available\n",
			       fdt_get_name(blob, child, NULL));
			continue;
		}
		ret = uclass_find_device_by_of_offset(UCLASS_VIDEO_CRTC, crtc_node, &crtc_dev);
		if (ret) {
			printf("Warn: %s: can't find crtc driver\n",
			       fdt_get_name(blob, child, NULL));
			continue;
		}

		crtc = (const struct rockchip_crtc *)dev_get_driver_data(crtc_dev);

		conn_node = find_connector_node(blob, connect);
		if (!fdt_device_is_available(blob, conn_node)) {
			printf("Warn: %s: connector node is not available\n",
			       fdt_get_name(blob, child, NULL));
			continue;
		}
		ret = uclass_get_device_by_of_offset(UCLASS_DISPLAY, conn_node, &conn_dev);
		if (ret) {
			printf("Warn: %s: can't find connector driver\n",
			       fdt_get_name(blob, child, NULL));
			continue;
		}
		conn = (const struct rockchip_connector *)dev_get_driver_data(conn_dev);

		s = malloc(sizeof(*s));
		if (!s)
			continue;

		memset(s, 0, sizeof(*s));

		INIT_LIST_HEAD(&s->head);
		s->ulogo_name = fdt_stringlist_get(blob, child, "logo,uboot", 0, NULL);
		s->klogo_name = fdt_stringlist_get(blob, child, "logo,kernel", 0, NULL);
		name = fdt_stringlist_get(blob, child, "logo,mode", 0, NULL);
		if (!strcmp(name, "fullscreen"))
			s->logo_mode = ROCKCHIP_DISPLAY_FULLSCREEN;
		else
			s->logo_mode = ROCKCHIP_DISPLAY_CENTER;
		name = fdt_stringlist_get(blob, child, "charge_logo,mode", 0, NULL);
		if (!strcmp(name, "fullscreen"))
			s->charge_logo_mode = ROCKCHIP_DISPLAY_FULLSCREEN;
		else
			s->charge_logo_mode = ROCKCHIP_DISPLAY_CENTER;

		s->blob = blob;
		s->conn_state.node = conn_node;
		s->conn_state.dev = conn_dev;
		s->conn_state.connector = conn;
		s->crtc_state.node = crtc_node;
		s->crtc_state.dev = crtc_dev;
		s->crtc_state.crtc = crtc;
		s->crtc_state.crtc_id = get_crtc_id(blob, connect);
		s->node = child;

		if (connector_phy_init(s)) {
			printf("Warn: %s: Failed to init phy drivers\n",
			       fdt_get_name(blob, child, NULL));
			free(s);
			continue;
		}

		if (connector_panel_init(s)) {
			printf("Warn: %s: Failed to init panel drivers\n",
			       fdt_get_name(blob, child, NULL));
			free(s);
			continue;
		}
		list_add_tail(&s->head, &rockchip_display_list);
	}

	if (list_empty(&rockchip_display_list)) {
		printf("Failed to found available display route\n");
		return -ENODEV;
	}

	uc_priv->xsize = DRM_ROCKCHIP_FB_WIDTH;
	uc_priv->ysize = DRM_ROCKCHIP_FB_HEIGHT;
	uc_priv->bpix = VIDEO_BPP32;

	#ifdef CONFIG_DRM_ROCKCHIP_VIDEO_FRAMEBUFFER
	rockchip_show_fbbase(plat->base);
	video_set_flush_dcache(dev, true);
	#endif

	return 0;
}

#if 0
void rockchip_display_fixup(void *blob)
{
	const struct rockchip_connector_funcs *conn_funcs;
	const struct rockchip_crtc_funcs *crtc_funcs;
	const struct rockchip_connector *conn;
	const struct rockchip_crtc *crtc;
	struct display_state *s;
	u32 offset;
	int node;
	char path[100];
	int ret;

	if (!get_display_size())
		return;

	node = fdt_update_reserved_memory(blob, "rockchip,drm-logo",
					       (u64)memory_start,
					       (u64)get_display_size());
	if (node < 0) {
		printf("failed to add drm-loader-logo memory\n");
		return;
	}

	list_for_each_entry(s, &rockchip_display_list, head) {
		conn = s->conn_state.connector;
		if (!conn)
			continue;
		conn_funcs = conn->funcs;
		if (!conn_funcs) {
			printf("failed to get exist connector\n");
			continue;
		}

		crtc = s->crtc_state.crtc;
		if (!crtc)
			continue;

		crtc_funcs = crtc->funcs;
		if (!crtc_funcs) {
			printf("failed to get exist crtc\n");
			continue;
		}

		if (crtc_funcs->fixup_dts)
			crtc_funcs->fixup_dts(s, blob);

		if (conn_funcs->fixup_dts)
			conn_funcs->fixup_dts(s, blob);

		ret = fdt_get_path(s->blob, s->node, path, sizeof(path));
		if (ret < 0) {
			printf("failed to get route path[%s], ret=%d\n",
			       path, ret);
			continue;
		}

#define FDT_SET_U32(name, val) \
		do_fixup_by_path_u32(blob, path, name, val, 1);

		offset = s->logo.offset + s->logo.mem - memory_start;
		FDT_SET_U32("logo,offset", offset);
		FDT_SET_U32("logo,width", s->logo.width);
		FDT_SET_U32("logo,height", s->logo.height);
		FDT_SET_U32("logo,bpp", s->logo.bpp);
		FDT_SET_U32("logo,ymirror", s->logo.ymirror);
		FDT_SET_U32("video,hdisplay", s->conn_state.mode.hdisplay);
		FDT_SET_U32("video,vdisplay", s->conn_state.mode.vdisplay);
		FDT_SET_U32("video,vrefresh",
			    drm_mode_vrefresh(&s->conn_state.mode));
#undef FDT_SET_U32
	}
}
#endif

int rockchip_display_bind(struct udevice *dev)
{
	struct video_uc_platdata *plat = dev_get_uclass_platdata(dev);

	plat->size = DRM_ROCKCHIP_FB_SIZE + MEMORY_POOL_SIZE;

	return 0;
}

static const struct udevice_id rockchip_display_ids[] = {
	{ .compatible = "rockchip,display-subsystem" },
	{ }
};

U_BOOT_DRIVER(rockchip_display) = {
	.name	= "rockchip_display",
	.id	= UCLASS_VIDEO,
	.of_match = rockchip_display_ids,
	.bind	= rockchip_display_bind,
	.probe	= rockchip_display_probe,
};

static int do_rockchip_logo_show(cmd_tbl_t *cmdtp, int flag, int argc,
			char *const argv[])
{
	if (argc != 1)
		return CMD_RET_USAGE;

	rockchip_show_logo();

	return 0;
}

static int do_rockchip_show_bmp(cmd_tbl_t *cmdtp, int flag, int argc,
				char *const argv[])
{
	if (argc != 2)
		return CMD_RET_USAGE;

	rockchip_show_bmp(argv[1]);

	return 0;
}

U_BOOT_CMD(
	rockchip_show_logo, 1, 1, do_rockchip_logo_show,
	"load and display log from resource partition",
	NULL
);

U_BOOT_CMD(
	rockchip_show_bmp, 2, 1, do_rockchip_show_bmp,
	"load and display bmp from resource partition",
	"    <bmp_name>"
);
