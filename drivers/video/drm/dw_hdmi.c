/*
 * (C) Copyright 2008-2017 Fuzhou Rockchip Electronics Co., Ltd
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

#include <common.h>
#include <fdtdec.h>
#include <fdt_support.h>
#include <malloc.h>
#include <syscon.h>
#include <asm/arch-rockchip/clock.h>
#include <edid.h>
#include <linux/hdmi.h>
#include <linux/media-bus-format.h>
#include <linux/dw_hdmi.h>
#include <asm/io.h>
#include "rockchip_display.h"
#include "rockchip_crtc.h"
#include "rockchip_connector.h"
#include "dw_hdmi.h"

/*
 * Unless otherwise noted, entries in this table are 100% optimization.
 * Values can be obtained from hdmi_compute_n() but that function is
 * slow so we pre-compute values we expect to see.
 *
 * All 32k and 48k values are expected to be the same (due to the way
 * the math works) for any rate that's an exact kHz.
 */
static const struct dw_hdmi_audio_tmds_n common_tmds_n_table[] = {
	{ .tmds = 25175000, .n_32k = 4096, .n_44k1 = 12854, .n_48k = 6144, },
	{ .tmds = 25200000, .n_32k = 4096, .n_44k1 = 5656, .n_48k = 6144, },
	{ .tmds = 27000000, .n_32k = 4096, .n_44k1 = 5488, .n_48k = 6144, },
	{ .tmds = 28320000, .n_32k = 4096, .n_44k1 = 5586, .n_48k = 6144, },
	{ .tmds = 30240000, .n_32k = 4096, .n_44k1 = 5642, .n_48k = 6144, },
	{ .tmds = 31500000, .n_32k = 4096, .n_44k1 = 5600, .n_48k = 6144, },
	{ .tmds = 32000000, .n_32k = 4096, .n_44k1 = 5733, .n_48k = 6144, },
	{ .tmds = 33750000, .n_32k = 4096, .n_44k1 = 6272, .n_48k = 6144, },
	{ .tmds = 36000000, .n_32k = 4096, .n_44k1 = 5684, .n_48k = 6144, },
	{ .tmds = 40000000, .n_32k = 4096, .n_44k1 = 5733, .n_48k = 6144, },
	{ .tmds = 49500000, .n_32k = 4096, .n_44k1 = 5488, .n_48k = 6144, },
	{ .tmds = 50000000, .n_32k = 4096, .n_44k1 = 5292, .n_48k = 6144, },
	{ .tmds = 54000000, .n_32k = 4096, .n_44k1 = 5684, .n_48k = 6144, },
	{ .tmds = 65000000, .n_32k = 4096, .n_44k1 = 7056, .n_48k = 6144, },
	{ .tmds = 68250000, .n_32k = 4096, .n_44k1 = 5376, .n_48k = 6144, },
	{ .tmds = 71000000, .n_32k = 4096, .n_44k1 = 7056, .n_48k = 6144, },
	{ .tmds = 72000000, .n_32k = 4096, .n_44k1 = 5635, .n_48k = 6144, },
	{ .tmds = 73250000, .n_32k = 4096, .n_44k1 = 14112, .n_48k = 6144, },
	{ .tmds = 74250000, .n_32k = 4096, .n_44k1 = 6272, .n_48k = 6144, },
	{ .tmds = 75000000, .n_32k = 4096, .n_44k1 = 5880, .n_48k = 6144, },
	{ .tmds = 78750000, .n_32k = 4096, .n_44k1 = 5600, .n_48k = 6144, },
	{ .tmds = 78800000, .n_32k = 4096, .n_44k1 = 5292, .n_48k = 6144, },
	{ .tmds = 79500000, .n_32k = 4096, .n_44k1 = 4704, .n_48k = 6144, },
	{ .tmds = 83500000, .n_32k = 4096, .n_44k1 = 7056, .n_48k = 6144, },
	{ .tmds = 85500000, .n_32k = 4096, .n_44k1 = 5488, .n_48k = 6144, },
	{ .tmds = 88750000, .n_32k = 4096, .n_44k1 = 14112, .n_48k = 6144, },
	{ .tmds = 97750000, .n_32k = 4096, .n_44k1 = 14112, .n_48k = 6144, },
	{ .tmds = 101000000, .n_32k = 4096, .n_44k1 = 7056, .n_48k = 6144, },
	{ .tmds = 106500000, .n_32k = 4096, .n_44k1 = 4704, .n_48k = 6144, },
	{ .tmds = 108000000, .n_32k = 4096, .n_44k1 = 5684, .n_48k = 6144, },
	{ .tmds = 115500000, .n_32k = 4096, .n_44k1 = 5712, .n_48k = 6144, },
	{ .tmds = 119000000, .n_32k = 4096, .n_44k1 = 5544, .n_48k = 6144, },
	{ .tmds = 135000000, .n_32k = 4096, .n_44k1 = 5488, .n_48k = 6144, },
	{ .tmds = 146250000, .n_32k = 4096, .n_44k1 = 6272, .n_48k = 6144, },
	{ .tmds = 148500000, .n_32k = 4096, .n_44k1 = 5488, .n_48k = 6144, },
	{ .tmds = 154000000, .n_32k = 4096, .n_44k1 = 5544, .n_48k = 6144, },
	{ .tmds = 162000000, .n_32k = 4096, .n_44k1 = 5684, .n_48k = 6144, },

	/* For 297 MHz+ HDMI spec have some other rule for setting N */
	{ .tmds = 297000000, .n_32k = 3073, .n_44k1 = 4704, .n_48k = 5120, },
	{ .tmds = 594000000, .n_32k = 3073, .n_44k1 = 9408, .n_48k = 10240, },

	/* End of table */
	{ .tmds = 0,         .n_32k = 0,    .n_44k1 = 0,    .n_48k = 0, },
};

static const u16 csc_coeff_default[3][4] = {
	{ 0x2000, 0x0000, 0x0000, 0x0000 },
	{ 0x0000, 0x2000, 0x0000, 0x0000 },
	{ 0x0000, 0x0000, 0x2000, 0x0000 }
};

static const u16 csc_coeff_rgb_out_eitu601[3][4] = {
	{ 0x2000, 0x6926, 0x74fd, 0x010e },
	{ 0x2000, 0x2cdd, 0x0000, 0x7e9a },
	{ 0x2000, 0x0000, 0x38b4, 0x7e3b }
};

static const u16 csc_coeff_rgb_out_eitu709[3][4] = {
	{ 0x2000, 0x7106, 0x7a02, 0x00a7 },
	{ 0x2000, 0x3264, 0x0000, 0x7e6d },
	{ 0x2000, 0x0000, 0x3b61, 0x7e25 }
};

static const u16 csc_coeff_rgb_in_eitu601[3][4] = {
	{ 0x2591, 0x1322, 0x074b, 0x0000 },
	{ 0x6535, 0x2000, 0x7acc, 0x0200 },
	{ 0x6acd, 0x7534, 0x2000, 0x0200 }
};

static const u16 csc_coeff_rgb_in_eitu709[3][4] = {
	{ 0x2dc5, 0x0d9b, 0x049e, 0x0000 },
	{ 0x62f0, 0x2000, 0x7d11, 0x0200 },
	{ 0x6756, 0x78ab, 0x2000, 0x0200 }
};

struct hdmi_vmode {
	bool mdataenablepolarity;

	unsigned int mpixelclock;
	unsigned int mpixelrepetitioninput;
	unsigned int mpixelrepetitionoutput;
};

struct hdmi_data_info {
	unsigned int enc_in_bus_format;
	unsigned int enc_out_bus_format;
	unsigned int enc_in_encoding;
	unsigned int enc_out_encoding;
	unsigned int pix_repet_factor;
	struct hdmi_vmode video_mode;
};

struct dw_hdmi_phy_data {
	enum dw_hdmi_phy_type type;
	const char *name;
	unsigned int gen;
	bool has_svsret;
	int (*configure)(struct dw_hdmi *hdmi,
			 const struct dw_hdmi_plat_data *pdata,
			 unsigned long mpixelclock);
};

struct dw_hdmi {
	enum dw_hdmi_devtype dev_type;
	unsigned int version;
	struct hdmi_data_info hdmi_data;
	struct hdmi_edid_data edid_data;
	const struct dw_hdmi_plat_data *plat_data;

	int vic;
	int io_width;

	unsigned long bus_format;
	bool cable_plugin;
	bool sink_is_hdmi;
	bool sink_has_audio;
	void *regs;
	void *grf;

	struct {
		const struct dw_hdmi_phy_ops *ops;
		const char *name;
		void *data;
		bool enabled;
	} phy;

	struct drm_display_mode previous_mode;

	unsigned int sample_rate;
	unsigned int audio_cts;
	unsigned int audio_n;
	bool audio_enable;

	void (*write)(struct dw_hdmi *hdmi, u8 val, int offset);
	u8 (*read)(struct dw_hdmi *hdmi, int offset);
};

static void dw_hdmi_writel(struct dw_hdmi *hdmi, u8 val, int offset)
{
	writel(val, hdmi->regs + (offset << 2));
}

static u8 dw_hdmi_readl(struct dw_hdmi *hdmi, int offset)
{
	return readl(hdmi->regs + (offset << 2));
}

static void dw_hdmi_writeb(struct dw_hdmi *hdmi, u8 val, int offset)
{
	writeb(val, hdmi->regs + offset);
}

static u8 dw_hdmi_readb(struct dw_hdmi *hdmi, int offset)
{
	return readb(hdmi->regs + offset);
}

static inline void hdmi_writeb(struct dw_hdmi *hdmi, u8 val, int offset)
{
	hdmi->write(hdmi, val, offset);
}

static inline u8 hdmi_readb(struct dw_hdmi *hdmi, int offset)
{
	return hdmi->read(hdmi, offset);
}

static void hdmi_modb(struct dw_hdmi *hdmi, u8 data, u8 mask, unsigned reg)
{
	u8 val = hdmi_readb(hdmi, reg) & ~mask;

	val |= data & mask;
	hdmi_writeb(hdmi, val, reg);
}

static void hdmi_mask_writeb(struct dw_hdmi *hdmi, u8 data, unsigned int reg,
			     u8 shift, u8 mask)
{
	hdmi_modb(hdmi, data << shift, mask, reg);
}

static bool hdmi_bus_fmt_is_rgb(unsigned int bus_format)
{
	switch (bus_format) {
	case MEDIA_BUS_FMT_RGB888_1X24:
	case MEDIA_BUS_FMT_RGB101010_1X30:
	case MEDIA_BUS_FMT_RGB121212_1X36:
	case MEDIA_BUS_FMT_RGB161616_1X48:
		return true;

	default:
		return false;
	}
}

static bool hdmi_bus_fmt_is_yuv444(unsigned int bus_format)
{
	switch (bus_format) {
	case MEDIA_BUS_FMT_YUV8_1X24:
	case MEDIA_BUS_FMT_YUV10_1X30:
	case MEDIA_BUS_FMT_YUV12_1X36:
	case MEDIA_BUS_FMT_YUV16_1X48:
		return true;

	default:
		return false;
	}
}

static bool hdmi_bus_fmt_is_yuv422(unsigned int bus_format)
{
	switch (bus_format) {
	case MEDIA_BUS_FMT_UYVY8_1X16:
	case MEDIA_BUS_FMT_UYVY10_1X20:
	case MEDIA_BUS_FMT_UYVY12_1X24:
		return true;

	default:
		return false;
	}
}

static bool hdmi_bus_fmt_is_yuv420(unsigned int bus_format)
{
	switch (bus_format) {
	case MEDIA_BUS_FMT_UYYVYY8_0_5X24:
	case MEDIA_BUS_FMT_UYYVYY10_0_5X30:
	case MEDIA_BUS_FMT_UYYVYY12_0_5X36:
	case MEDIA_BUS_FMT_UYYVYY16_0_5X48:
		return true;

	default:
		return false;
	}
}

static int hdmi_bus_fmt_color_depth(unsigned int bus_format)
{
	switch (bus_format) {
	case MEDIA_BUS_FMT_RGB888_1X24:
	case MEDIA_BUS_FMT_YUV8_1X24:
	case MEDIA_BUS_FMT_UYVY8_1X16:
	case MEDIA_BUS_FMT_UYYVYY8_0_5X24:
		return 8;

	case MEDIA_BUS_FMT_RGB101010_1X30:
	case MEDIA_BUS_FMT_YUV10_1X30:
	case MEDIA_BUS_FMT_UYVY10_1X20:
	case MEDIA_BUS_FMT_UYYVYY10_0_5X30:
		return 10;

	case MEDIA_BUS_FMT_RGB121212_1X36:
	case MEDIA_BUS_FMT_YUV12_1X36:
	case MEDIA_BUS_FMT_UYVY12_1X24:
	case MEDIA_BUS_FMT_UYYVYY12_0_5X36:
		return 12;

	case MEDIA_BUS_FMT_RGB161616_1X48:
	case MEDIA_BUS_FMT_YUV16_1X48:
	case MEDIA_BUS_FMT_UYYVYY16_0_5X48:
		return 16;

	default:
		return 0;
	}
}

static int is_color_space_conversion(struct dw_hdmi *hdmi)
{
	return hdmi->hdmi_data.enc_in_bus_format !=
	hdmi->hdmi_data.enc_out_bus_format;
}

static int is_color_space_decimation(struct dw_hdmi *hdmi)
{
	if (!hdmi_bus_fmt_is_yuv422(hdmi->hdmi_data.enc_out_bus_format))
		return 0;

	if (hdmi_bus_fmt_is_rgb(hdmi->hdmi_data.enc_in_bus_format) ||
	    hdmi_bus_fmt_is_yuv444(hdmi->hdmi_data.enc_in_bus_format))
		return 1;

	return 0;
}

static inline void hdmi_phy_test_clear(struct dw_hdmi *hdmi,
				       unsigned char bit)
{
	hdmi_modb(hdmi, bit << HDMI_PHY_TST0_TSTCLR_OFFSET,
		  HDMI_PHY_TST0_TSTCLR_MASK, HDMI_PHY_TST0);
}

static inline void hdmi_phy_test_enable(struct dw_hdmi *hdmi,
					unsigned char bit)
{
	hdmi_modb(hdmi, bit << HDMI_PHY_TST0_TSTEN_OFFSET,
		  HDMI_PHY_TST0_TSTEN_MASK, HDMI_PHY_TST0);
}

static inline void hdmi_phy_test_clock(struct dw_hdmi *hdmi,
				       unsigned char bit)
{
	hdmi_modb(hdmi, bit << HDMI_PHY_TST0_TSTCLK_OFFSET,
		  HDMI_PHY_TST0_TSTCLK_MASK, HDMI_PHY_TST0);
}

static inline void hdmi_phy_test_din(struct dw_hdmi *hdmi,
				     unsigned char bit)
{
	hdmi_writeb(hdmi, bit, HDMI_PHY_TST1);
}

static inline void hdmi_phy_test_dout(struct dw_hdmi *hdmi,
				      unsigned char bit)
{
	hdmi_writeb(hdmi, bit, HDMI_PHY_TST2);
}

static bool hdmi_phy_wait_i2c_done(struct dw_hdmi *hdmi, int msec)
{
	u32 val;

	while ((val = hdmi_readb(hdmi, HDMI_IH_I2CMPHY_STAT0) & 0x3) == 0) {
		if (msec-- == 0)
			return false;
		udelay(1000);
	}
	hdmi_writeb(hdmi, val, HDMI_IH_I2CMPHY_STAT0);

	return true;
}

static void dw_hdmi_phy_i2c_write(struct dw_hdmi *hdmi, unsigned short data,
				  unsigned char addr)
{
	hdmi_writeb(hdmi, 0xFF, HDMI_IH_I2CMPHY_STAT0);
	hdmi_writeb(hdmi, addr, HDMI_PHY_I2CM_ADDRESS_ADDR);
	hdmi_writeb(hdmi, (unsigned char)(data >> 8),
		    HDMI_PHY_I2CM_DATAO_1_ADDR);
	hdmi_writeb(hdmi, (unsigned char)(data >> 0),
		    HDMI_PHY_I2CM_DATAO_0_ADDR);
	hdmi_writeb(hdmi, HDMI_PHY_I2CM_OPERATION_ADDR_WRITE,
		    HDMI_PHY_I2CM_OPERATION_ADDR);
	hdmi_phy_wait_i2c_done(hdmi, 1000);
}

static void dw_hdmi_phy_enable_powerdown(struct dw_hdmi *hdmi, bool enable)
{
	hdmi_mask_writeb(hdmi, !enable, HDMI_PHY_CONF0,
			 HDMI_PHY_CONF0_PDZ_OFFSET,
			 HDMI_PHY_CONF0_PDZ_MASK);
}

static void dw_hdmi_phy_enable_tmds(struct dw_hdmi *hdmi, u8 enable)
{
	hdmi_mask_writeb(hdmi, enable, HDMI_PHY_CONF0,
			 HDMI_PHY_CONF0_ENTMDS_OFFSET,
			 HDMI_PHY_CONF0_ENTMDS_MASK);
}

static void dw_hdmi_phy_enable_svsret(struct dw_hdmi *hdmi, u8 enable)
{
	hdmi_mask_writeb(hdmi, enable, HDMI_PHY_CONF0,
			 HDMI_PHY_CONF0_SVSRET_OFFSET,
			 HDMI_PHY_CONF0_SVSRET_MASK);
}

static void dw_hdmi_phy_gen2_pddq(struct dw_hdmi *hdmi, u8 enable)
{
	hdmi_mask_writeb(hdmi, enable, HDMI_PHY_CONF0,
			 HDMI_PHY_CONF0_GEN2_PDDQ_OFFSET,
			 HDMI_PHY_CONF0_GEN2_PDDQ_MASK);
}

static void dw_hdmi_phy_gen2_txpwron(struct dw_hdmi *hdmi, u8 enable)
{
	hdmi_mask_writeb(hdmi, enable, HDMI_PHY_CONF0,
			 HDMI_PHY_CONF0_GEN2_TXPWRON_OFFSET,
			 HDMI_PHY_CONF0_GEN2_TXPWRON_MASK);
}

static void dw_hdmi_phy_sel_data_en_pol(struct dw_hdmi *hdmi, u8 enable)
{
	hdmi_mask_writeb(hdmi, enable, HDMI_PHY_CONF0,
			 HDMI_PHY_CONF0_SELDATAENPOL_OFFSET,
			 HDMI_PHY_CONF0_SELDATAENPOL_MASK);
}

static void dw_hdmi_phy_sel_interface_control(struct dw_hdmi *hdmi, u8 enable)
{
	hdmi_mask_writeb(hdmi, enable, HDMI_PHY_CONF0,
			 HDMI_PHY_CONF0_SELDIPIF_OFFSET,
			 HDMI_PHY_CONF0_SELDIPIF_MASK);
}

static void dw_hdmi_phy_power_off(struct dw_hdmi *hdmi)
{
	const struct dw_hdmi_phy_data *phy = hdmi->phy.data;
	unsigned int i;
	u16 val;

	if (phy->gen == 1) {
		dw_hdmi_phy_enable_tmds(hdmi, 0);
		dw_hdmi_phy_enable_powerdown(hdmi, true);
		return;
	}

	dw_hdmi_phy_gen2_txpwron(hdmi, 0);

	/*
	 * Wait for TX_PHY_LOCK to be deasserted to indicate that the PHY went
	 * to low power mode.
	 */
	for (i = 0; i < 5; ++i) {
		val = hdmi_readb(hdmi, HDMI_PHY_STAT0);
		if (!(val & HDMI_PHY_TX_PHY_LOCK))
			break;

		udelay(2000);
	}

	if (val & HDMI_PHY_TX_PHY_LOCK)
		printf("PHY failed to power down\n");
	else
		printf("PHY powered down in %u iterations\n", i);

	dw_hdmi_phy_gen2_pddq(hdmi, 1);
}

static int dw_hdmi_phy_power_on(struct dw_hdmi *hdmi)
{
	const struct dw_hdmi_phy_data *phy = hdmi->phy.data;
	unsigned int i;
	u8 val;

	if (phy->gen == 1) {
		dw_hdmi_phy_enable_powerdown(hdmi, false);

		/* Toggle TMDS enable. */
		dw_hdmi_phy_enable_tmds(hdmi, 0);
		dw_hdmi_phy_enable_tmds(hdmi, 1);
		return 0;
	}

	dw_hdmi_phy_gen2_txpwron(hdmi, 1);
	dw_hdmi_phy_gen2_pddq(hdmi, 0);

	/* Wait for PHY PLL lock */
	for (i = 0; i < 5; ++i) {
		val = hdmi_readb(hdmi, HDMI_PHY_STAT0) & HDMI_PHY_TX_PHY_LOCK;
		if (val)
			break;

		udelay(2000);
	}

	if (!val) {
		printf("PHY PLL failed to lock\n");
		return -ETIMEDOUT;
	}

	printf("PHY PLL locked %u iterations\n", i);
	return 0;
}

/*
 * PHY configuration function for the DWC HDMI 3D TX PHY. Based on the available
 * information the DWC MHL PHY has the same register layout and is thus also
 * supported by this function.
 */
static
int hdmi_phy_configure_dwc_hdmi_3d_tx(struct dw_hdmi *hdmi,
				      const struct dw_hdmi_plat_data *pdata,
				      unsigned long mpixelclock)
{
	const struct dw_hdmi_mpll_config *mpll_config = pdata->mpll_cfg;
	const struct dw_hdmi_curr_ctrl *curr_ctrl = pdata->cur_ctr;
	const struct dw_hdmi_phy_config *phy_config = pdata->phy_config;

	/* PLL/MPLL Cfg - always match on final entry */
	for (; mpll_config->mpixelclock != ~0UL; mpll_config++)
		if (mpixelclock <= mpll_config->mpixelclock)
			break;

	for (; curr_ctrl->mpixelclock != ~0UL; curr_ctrl++)
		if (mpixelclock <= curr_ctrl->mpixelclock)
			break;

	for (; phy_config->mpixelclock != ~0UL; phy_config++)
		if (mpixelclock <= phy_config->mpixelclock)
			break;

	if (mpll_config->mpixelclock == ~0UL ||
	    curr_ctrl->mpixelclock == ~0UL ||
	    phy_config->mpixelclock == ~0UL)
		return -EINVAL;

	/*
	 * RK3399 mpll clock source is vpll, also is vop clock source.
	 * vpll rate is twice of mpixelclock in YCBCR420 mode, we need
	 * to enable mpll pre-divider.
	 */
	if (hdmi_bus_fmt_is_yuv420(hdmi->hdmi_data.enc_out_bus_format) &&
	    (hdmi->dev_type == RK3399_HDMI || hdmi->dev_type == RK3368_HDMI))
		dw_hdmi_phy_i2c_write(hdmi, mpll_config->res[0].cpce | 4,
				      HDMI_3D_TX_PHY_CPCE_CTRL);
	else
		dw_hdmi_phy_i2c_write(hdmi, mpll_config->res[0].cpce,
				      HDMI_3D_TX_PHY_CPCE_CTRL);
	dw_hdmi_phy_i2c_write(hdmi, mpll_config->res[0].gmp,
			      HDMI_3D_TX_PHY_GMPCTRL);
	dw_hdmi_phy_i2c_write(hdmi, curr_ctrl->curr[0],
			      HDMI_3D_TX_PHY_CURRCTRL);

	dw_hdmi_phy_i2c_write(hdmi, 0, HDMI_3D_TX_PHY_PLLPHBYCTRL);
	dw_hdmi_phy_i2c_write(hdmi, HDMI_3D_TX_PHY_MSM_CTRL_CKO_SEL_FB_CLK,
			      HDMI_3D_TX_PHY_MSM_CTRL);

	dw_hdmi_phy_i2c_write(hdmi, 0x0004, HDMI_3D_TX_PHY_TXTERM);
	dw_hdmi_phy_i2c_write(hdmi, 0x8009,
			      HDMI_3D_TX_PHY_CKSYMTXCTRL);
	dw_hdmi_phy_i2c_write(hdmi, 0x0272,
			      HDMI_3D_TX_PHY_VLEVCTRL);

	/* Override and disable clock termination. */
	dw_hdmi_phy_i2c_write(hdmi, HDMI_3D_TX_PHY_CKCALCTRL_OVERRIDE,
			      HDMI_3D_TX_PHY_CKCALCTRL);
	return 0;
}

static const struct dw_hdmi_phy_data dw_hdmi_phys[] = {
	{
		.type = DW_HDMI_PHY_DWC_HDMI_TX_PHY,
		.name = "DWC HDMI TX PHY",
		.gen = 1,
	}, {
		.type = DW_HDMI_PHY_DWC_MHL_PHY_HEAC,
		.name = "DWC MHL PHY + HEAC PHY",
		.gen = 2,
		.has_svsret = true,
		.configure = hdmi_phy_configure_dwc_hdmi_3d_tx,
	}, {
		.type = DW_HDMI_PHY_DWC_MHL_PHY,
		.name = "DWC MHL PHY",
		.gen = 2,
		.has_svsret = true,
		.configure = hdmi_phy_configure_dwc_hdmi_3d_tx,
	}, {
		.type = DW_HDMI_PHY_DWC_HDMI_3D_TX_PHY_HEAC,
		.name = "DWC HDMI 3D TX PHY + HEAC PHY",
		.gen = 2,
		.configure = hdmi_phy_configure_dwc_hdmi_3d_tx,
	}, {
		.type = DW_HDMI_PHY_DWC_HDMI_3D_TX_PHY,
		.name = "DWC HDMI 3D TX PHY",
		.gen = 2,
		.configure = hdmi_phy_configure_dwc_hdmi_3d_tx,
	}, {
		.type = DW_HDMI_PHY_DWC_HDMI20_TX_PHY,
		.name = "DWC HDMI 2.0 TX PHY",
		.gen = 2,
		.has_svsret = true,
		.configure = hdmi_phy_configure_dwc_hdmi_3d_tx,
	}, {
		.type = DW_HDMI_PHY_VENDOR_PHY,
		.name = "Vendor PHY",
	}
};

/* ddc i2c master reset */
static void rockchip_dw_hdmi_i2cm_reset(struct dw_hdmi *hdmi)
{
	hdmi_writeb(hdmi, 0x00, HDMI_I2CM_SOFTRSTZ);
	udelay(100);
}

static void rockchip_dw_hdmi_i2cm_mask_int(struct dw_hdmi *hdmi, int mask)
{
	if (!mask) {
		hdmi_writeb(hdmi, HDMI_I2CM_INT_DONE_POL, HDMI_I2CM_INT);
		hdmi_writeb(hdmi, HDMI_I2CM_CTLINT_NAC_POL |
			    HDMI_I2CM_CTLINT_ARB_POL, HDMI_I2CM_CTLINT);
		hdmi_writeb(hdmi, 0x00, HDMI_IH_MUTE_I2CM_STAT0);
	} else {
		hdmi_writeb(hdmi, 0xff, HDMI_I2CM_INT);
		hdmi_writeb(hdmi, 0xff, HDMI_I2CM_CTLINT);
	}
}

static u16 i2c_count(u16 sfrclock, u16 sclmintime)
{
	unsigned long tmp_scl_period = 0;

	if (((sfrclock * sclmintime) % I2C_DIV_FACTOR) != 0)
		tmp_scl_period = (unsigned long)((sfrclock * sclmintime) +
				(I2C_DIV_FACTOR - ((sfrclock * sclmintime) %
				I2C_DIV_FACTOR))) / I2C_DIV_FACTOR;
	else
		tmp_scl_period = (unsigned long)(sfrclock * sclmintime) /
				I2C_DIV_FACTOR;

	return (u16)(tmp_scl_period);
}

static void rockchip_dw_hdmi_i2cm_clk_init(struct dw_hdmi *hdmi)
{
	int value;

	/* Set DDC I2C CLK which divided from DDC_CLK. */
	value = i2c_count(24000, EDID_I2C_MIN_SS_SCL_HIGH_TIME);
	hdmi_writeb(hdmi, value & 0xff,
		    HDMI_I2CM_SS_SCL_HCNT_0_ADDR);
	hdmi_writeb(hdmi, (value >> 8) & 0xff,
		    HDMI_I2CM_SS_SCL_HCNT_1_ADDR);
	value = i2c_count(24000, EDID_I2C_MIN_SS_SCL_LOW_TIME);
	hdmi_writeb(hdmi, value & 0xff,
		    HDMI_I2CM_SS_SCL_LCNT_0_ADDR);
	hdmi_writeb(hdmi, (value >> 8) & 0xff,
		    HDMI_I2CM_SS_SCL_LCNT_1_ADDR);
	hdmi_modb(hdmi, HDMI_I2CM_DIV_STD_MODE,
		  HDMI_I2CM_DIV_FAST_STD_MODE, HDMI_I2CM_DIV);
}

/*set read/write offset,set read/write mode*/
static void rockchip_dw_hdmi_i2cm_write_request(struct dw_hdmi *hdmi,
						u8 offset, u8 data)
{
	hdmi_writeb(hdmi, offset, HDMI_I2CM_ADDRESS);
	hdmi_writeb(hdmi, data, HDMI_I2CM_DATAO);
	hdmi_writeb(hdmi, HDMI_I2CM_OPERATION_WRITE, HDMI_I2CM_OPERATION_READ);
}

static void rockchip_dw_hdmi_i2cm_read_request(struct dw_hdmi *hdmi,
					       u8 offset)
{
	hdmi_writeb(hdmi, offset, HDMI_I2CM_ADDRESS);
	hdmi_writeb(hdmi, HDMI_I2CM_OPERATION_READ, HDMI_I2CM_OPERATION);
}

static void rockchip_dw_hdmi_i2cm_write_data(struct dw_hdmi *hdmi,
					     u8 data, u8 offset)
{
	u8 interrupt = 0;
	int trytime = 2;
	int i = 20;

	while (trytime-- > 0) {
		rockchip_dw_hdmi_i2cm_write_request(hdmi, offset, data);
		while (i--) {
			udelay(1000);
			interrupt = hdmi_readb(hdmi, HDMI_IH_I2CM_STAT0);
			if (interrupt)
				hdmi_writeb(hdmi,
					    interrupt, HDMI_IH_I2CM_STAT0);

			if (interrupt & (m_SCDC_READREQ |
					 m_I2CM_DONE | m_I2CM_ERROR))
				break;
		}

		if (interrupt & m_I2CM_DONE) {
			printf("[%s] write offset %02x data %02x success\n",
			       __func__, offset, data);
			trytime = 0;
		} else if ((interrupt & m_I2CM_ERROR) || (i == -1)) {
			printf("[%s] write data error\n", __func__);
			rockchip_dw_hdmi_i2cm_reset(hdmi);
		}
	}
}

static int rockchip_dw_hdmi_i2cm_read_data(struct dw_hdmi *hdmi, u8 offset)
{
	u8 interrupt = 0, val = 0;
	int trytime = 2;
	int i = 20;

	while (trytime-- > 0) {
		rockchip_dw_hdmi_i2cm_read_request(hdmi, offset);
		while (i--) {
			udelay(1000);
			interrupt = hdmi_readb(hdmi, HDMI_IH_I2CM_STAT0);
			if (interrupt)
				hdmi_writeb(hdmi, HDMI_IH_I2CM_STAT0,
					    interrupt);

			if (interrupt & (m_SCDC_READREQ |
				m_I2CM_DONE | m_I2CM_ERROR))
				break;
		}

		if (interrupt & m_I2CM_DONE) {
			val = hdmi_readb(hdmi, HDMI_I2CM_DATAI);
			trytime = 0;
		} else if ((interrupt & m_I2CM_ERROR) || (i == -1)) {
			printf("[%s] read data error\n", __func__);
			rockchip_dw_hdmi_i2cm_reset(hdmi);
		}
	}
	return val;
}

static int rockchip_dw_hdmi_scdc_get_sink_version(struct dw_hdmi *hdmi)
{
	return rockchip_dw_hdmi_i2cm_read_data(hdmi, SCDC_SINK_VERSION);
}

static void rockchip_dw_hdmi_scdc_set_source_version(struct dw_hdmi *hdmi,
						     u8 version)
{
	rockchip_dw_hdmi_i2cm_write_data(hdmi, version, SCDC_SOURCE_VERSION);
}

static void rockchip_dw_hdmi_scdc_init(struct dw_hdmi *hdmi)
{
	rockchip_dw_hdmi_i2cm_reset(hdmi);
	rockchip_dw_hdmi_i2cm_mask_int(hdmi, 1);
	rockchip_dw_hdmi_i2cm_clk_init(hdmi);
	/* set scdc i2c addr */
	hdmi_writeb(hdmi, DDC_I2C_SCDC_ADDR, HDMI_I2CM_SLAVE);
	rockchip_dw_hdmi_i2cm_mask_int(hdmi, 0);/*enable interrupt*/
}

static int rockchip_dw_hdmi_scrambling_enable(struct dw_hdmi *hdmi,
					      int enable)
{
	int stat;

	rockchip_dw_hdmi_scdc_init(hdmi);
	stat = rockchip_dw_hdmi_i2cm_read_data(hdmi,
					       SCDC_TMDS_CONFIG);
	if (stat < 0) {
		debug("Failed to read tmds config\n");
		return false;
	}

	if (enable == 1) {
		/* Write on Rx the bit Scrambling_Enable, register 0x20 */
		stat |= SCDC_SCRAMBLING_ENABLE;
		rockchip_dw_hdmi_i2cm_write_data(hdmi, stat, SCDC_TMDS_CONFIG);
		/* TMDS software reset request */
		hdmi_writeb(hdmi, (u8)~HDMI_MC_SWRSTZ_TMDSSWRST_REQ,
			    HDMI_MC_SWRSTZ);
		/* Enable/Disable Scrambling */
		hdmi_writeb(hdmi, 1, HDMI_FC_SCRAMBLER_CTRL);
	} else {
		/* Enable/Disable Scrambling */
		hdmi_writeb(hdmi, 0, HDMI_FC_SCRAMBLER_CTRL);
		/* TMDS software reset request */
		hdmi_writeb(hdmi, (u8)~HDMI_MC_SWRSTZ_TMDSSWRST_REQ,
			    HDMI_MC_SWRSTZ);
		/* Write on Rx the bit Scrambling_Enable, register 0x20 */
		stat &= ~SCDC_SCRAMBLING_ENABLE;
		rockchip_dw_hdmi_i2cm_write_data(hdmi, stat, SCDC_TMDS_CONFIG);
	}

	return 0;
}

static void rockchip_dw_hdmi_scdc_set_tmds_rate(struct dw_hdmi *hdmi)
{
	int stat;

	rockchip_dw_hdmi_scdc_init(hdmi);
	stat = rockchip_dw_hdmi_i2cm_read_data(hdmi,
					       SCDC_TMDS_CONFIG);
	if (hdmi->hdmi_data.video_mode.mpixelclock > 340000000)
		stat |= SCDC_TMDS_BIT_CLOCK_RATIO_BY_40;
	else
		stat &= ~SCDC_TMDS_BIT_CLOCK_RATIO_BY_40;
	rockchip_dw_hdmi_i2cm_write_data(hdmi, stat,
					 SCDC_TMDS_CONFIG);
}

static int hdmi_phy_configure(struct dw_hdmi *hdmi)
{
	const struct dw_hdmi_phy_data *phy = hdmi->phy.data;
	const struct dw_hdmi_plat_data *pdata = hdmi->plat_data;
	unsigned long mpixelclock = hdmi->hdmi_data.video_mode.mpixelclock;
	int ret, sink_version;

	dw_hdmi_phy_power_off(hdmi);

	/* Control for TMDS Bit Period/TMDS Clock-Period Ratio */
	if (hdmi->edid_data.display_info.hdmi.scdc.supported)
		rockchip_dw_hdmi_scdc_set_tmds_rate(hdmi);

	/* Leave low power consumption mode by asserting SVSRET. */
	if (phy->has_svsret)
		dw_hdmi_phy_enable_svsret(hdmi, 1);

	/* PHY reset. The reset signal is active high on Gen2 PHYs. */
	hdmi_writeb(hdmi, HDMI_MC_PHYRSTZ_PHYRSTZ, HDMI_MC_PHYRSTZ);
	hdmi_writeb(hdmi, 0, HDMI_MC_PHYRSTZ);

	hdmi_writeb(hdmi, HDMI_MC_HEACPHY_RST_ASSERT, HDMI_MC_HEACPHY_RST);

	hdmi_phy_test_clear(hdmi, 1);
	hdmi_writeb(hdmi, HDMI_PHY_I2CM_SLAVE_ADDR_PHY_GEN2,
		    HDMI_PHY_I2CM_SLAVE_ADDR);
	hdmi_phy_test_clear(hdmi, 0);

	/* Write to the PHY as configured by the platform */
	if (pdata->configure_phy)
		ret = pdata->configure_phy(hdmi, pdata, mpixelclock);
	else
		ret = phy->configure(hdmi, pdata, mpixelclock);
	if (ret) {
		printf("PHY configuration failed (clock %lu)\n",
		       mpixelclock);
		return ret;
	}

	/* Wait for resuming transmission of TMDS clock and data */
	if (mpixelclock > 340000000)
		mdelay(100);

	return dw_hdmi_phy_power_on(hdmi);
}

static int dw_hdmi_phy_init(struct dw_hdmi *hdmi, void *data,
			    struct drm_display_mode *mode)
{
	int i, ret;

	/* HDMI Phy spec says to do the phy initialization sequence twice */
	for (i = 0; i < 2; i++) {
		dw_hdmi_phy_sel_data_en_pol(hdmi, 1);
		dw_hdmi_phy_sel_interface_control(hdmi, 0);
		ret = hdmi_phy_configure(hdmi);
		if (ret)
			return ret;
	}

	return 0;
}

static void dw_hdmi_phy_disable(struct dw_hdmi *hdmi, void *data)
{
	dw_hdmi_phy_power_off(hdmi);
}

static enum drm_connector_status dw_hdmi_phy_read_hpd(struct dw_hdmi *hdmi,
						      void *data)
{
	return hdmi_readb(hdmi, HDMI_PHY_STAT0) & HDMI_PHY_HPD ?
		connector_status_connected : connector_status_disconnected;
}

static const struct dw_hdmi_phy_ops dw_hdmi_synopsys_phy_ops = {
	.init = dw_hdmi_phy_init,
	.disable = dw_hdmi_phy_disable,
	.read_hpd = dw_hdmi_phy_read_hpd,
};

static int dw_hdmi_detect_phy(struct dw_hdmi *hdmi)
{
	unsigned int i;
	u8 phy_type;

	phy_type = hdmi_readb(hdmi, HDMI_CONFIG2_ID);

	/*
	 * RK3228 and RK3328 phy_type is DW_HDMI_PHY_DWC_HDMI20_TX_PHY,
	 * but it has a vedor phy.
	 */
	if (phy_type == DW_HDMI_PHY_VENDOR_PHY ||
	    hdmi->dev_type == RK3328_HDMI ||
	    hdmi->dev_type == RK3228_HDMI) {
		/* Vendor PHYs require support from the glue layer. */
		if (!hdmi->plat_data->phy_ops || !hdmi->plat_data->phy_name) {
			printf(
				"Vendor HDMI PHY not supported by glue layer\n");
			return -ENODEV;
		}

		hdmi->phy.ops = hdmi->plat_data->phy_ops;
		hdmi->phy.data = hdmi->plat_data->phy_data;
		hdmi->phy.name = hdmi->plat_data->phy_name;
		return 0;
	}

	/* Synopsys PHYs are handled internally. */
	for (i = 0; i < ARRAY_SIZE(dw_hdmi_phys); ++i) {
		if (dw_hdmi_phys[i].type == phy_type) {
			hdmi->phy.ops = &dw_hdmi_synopsys_phy_ops;
			hdmi->phy.name = dw_hdmi_phys[i].name;
			hdmi->phy.data = (void *)&dw_hdmi_phys[i];

			if (!dw_hdmi_phys[i].configure &&
			    !hdmi->plat_data->configure_phy) {
				printf("%s requires platform support\n",
				       hdmi->phy.name);
				return -ENODEV;
			}

			return 0;
		}
	}

	printf("Unsupported HDMI PHY type (%02x)\n", phy_type);
	return -ENODEV;
}

static void hdmi_av_composer(struct dw_hdmi *hdmi,
			     const struct drm_display_mode *mode)
{
	u8 inv_val = 0;
	struct hdmi_vmode *vmode = &hdmi->hdmi_data.video_mode;
	struct drm_hdmi_info *hdmi_info = &hdmi->edid_data.display_info.hdmi;
	int bytes, hblank, vblank, h_de_hs, v_de_vs, hsync_len, vsync_len;
	unsigned int hdisplay, vdisplay;

	vmode->mpixelclock = mode->clock * 1000;
	if (hdmi_bus_fmt_is_yuv420(hdmi->hdmi_data.enc_out_bus_format))
		vmode->mpixelclock /= 2;
	if ((mode->flags & DRM_MODE_FLAG_3D_MASK) ==
		DRM_MODE_FLAG_3D_FRAME_PACKING)
		vmode->mpixelclock *= 2;
	printf("final pixclk = %d\n", vmode->mpixelclock);

	/* Set up HDMI_FC_INVIDCONF
	 * fc_invidconf.HDCP_keepout must be set (1'b1)
	 * when activate the scrambler feature.
	 */
	inv_val = (vmode->mpixelclock > 340000000 ||
		   hdmi_info->scdc.scrambling.low_rates ?
		   HDMI_FC_INVIDCONF_HDCP_KEEPOUT_ACTIVE :
		   HDMI_FC_INVIDCONF_HDCP_KEEPOUT_INACTIVE);

	inv_val |= mode->flags & DRM_MODE_FLAG_PVSYNC ?
		HDMI_FC_INVIDCONF_VSYNC_IN_POLARITY_ACTIVE_HIGH :
		HDMI_FC_INVIDCONF_VSYNC_IN_POLARITY_ACTIVE_LOW;

	inv_val |= mode->flags & DRM_MODE_FLAG_PHSYNC ?
		HDMI_FC_INVIDCONF_HSYNC_IN_POLARITY_ACTIVE_HIGH :
		HDMI_FC_INVIDCONF_HSYNC_IN_POLARITY_ACTIVE_LOW;

	inv_val |= (vmode->mdataenablepolarity ?
		HDMI_FC_INVIDCONF_DE_IN_POLARITY_ACTIVE_HIGH :
		HDMI_FC_INVIDCONF_DE_IN_POLARITY_ACTIVE_LOW);

	if (hdmi->vic == 39)
		inv_val |= HDMI_FC_INVIDCONF_R_V_BLANK_IN_OSC_ACTIVE_HIGH;
	else
		inv_val |= mode->flags & DRM_MODE_FLAG_INTERLACE ?
			HDMI_FC_INVIDCONF_R_V_BLANK_IN_OSC_ACTIVE_HIGH :
			HDMI_FC_INVIDCONF_R_V_BLANK_IN_OSC_ACTIVE_LOW;

	inv_val |= mode->flags & DRM_MODE_FLAG_INTERLACE ?
		HDMI_FC_INVIDCONF_IN_I_P_INTERLACED :
		HDMI_FC_INVIDCONF_IN_I_P_PROGRESSIVE;

	inv_val |= hdmi->sink_is_hdmi ?
		HDMI_FC_INVIDCONF_DVI_MODEZ_HDMI_MODE :
		HDMI_FC_INVIDCONF_DVI_MODEZ_DVI_MODE;

	hdmi_writeb(hdmi, inv_val, HDMI_FC_INVIDCONF);

	hdisplay = mode->hdisplay;
	hblank = mode->htotal - mode->hdisplay;
	h_de_hs = mode->hsync_start - mode->hdisplay;
	hsync_len = mode->hsync_end - mode->hsync_start;

	/*
	 * When we're setting a YCbCr420 mode, we need
	 * to adjust the horizontal timing to suit.
	 */
	/*
	 * When we're setting a YCbCr420 mode, we need
	 * to adjust the horizontal timing to suit.
	 */
	if (hdmi_bus_fmt_is_yuv420(hdmi->hdmi_data.enc_out_bus_format)) {
		hdisplay /= 2;
		hblank /= 2;
		h_de_hs /= 2;
		hsync_len /= 2;
	}

	vdisplay = mode->vdisplay;
	vblank = mode->vtotal - mode->vdisplay;
	v_de_vs = mode->vsync_start - mode->vdisplay;
	vsync_len = mode->vsync_end - mode->vsync_start;

	/*
	 * When we're setting an interlaced mode, we need
	 * to adjust the vertical timing to suit.
	 */
	if (mode->flags & DRM_MODE_FLAG_INTERLACE) {
		vdisplay /= 2;
		vblank /= 2;
		v_de_vs /= 2;
		vsync_len /= 2;
	} else if ((mode->flags & DRM_MODE_FLAG_3D_MASK) ==
		DRM_MODE_FLAG_3D_FRAME_PACKING) {
		vdisplay += mode->vtotal;
	}

	/* Scrambling Control */
	if (hdmi_info->scdc.supported) {
		if (vmode->mpixelclock > 340000000 ||
		    hdmi_info->scdc.scrambling.low_rates) {
			bytes = rockchip_dw_hdmi_scdc_get_sink_version(hdmi);
			rockchip_dw_hdmi_scdc_set_source_version(hdmi, bytes);
			rockchip_dw_hdmi_scrambling_enable(hdmi, 1);
		} else {
			rockchip_dw_hdmi_scrambling_enable(hdmi, 0);
		}
	}

	/* Set up horizontal active pixel width */
	hdmi_writeb(hdmi, hdisplay >> 8, HDMI_FC_INHACTV1);
	hdmi_writeb(hdmi, hdisplay, HDMI_FC_INHACTV0);

	/* Set up vertical active lines */
	hdmi_writeb(hdmi, vdisplay >> 8, HDMI_FC_INVACTV1);
	hdmi_writeb(hdmi, vdisplay, HDMI_FC_INVACTV0);

	/* Set up horizontal blanking pixel region width */
	hdmi_writeb(hdmi, hblank >> 8, HDMI_FC_INHBLANK1);
	hdmi_writeb(hdmi, hblank, HDMI_FC_INHBLANK0);

	/* Set up vertical blanking pixel region width */
	hdmi_writeb(hdmi, vblank, HDMI_FC_INVBLANK);

	/* Set up HSYNC active edge delay width (in pixel clks) */
	hdmi_writeb(hdmi, h_de_hs >> 8, HDMI_FC_HSYNCINDELAY1);
	hdmi_writeb(hdmi, h_de_hs, HDMI_FC_HSYNCINDELAY0);

	/* Set up VSYNC active edge delay (in lines) */
	hdmi_writeb(hdmi, v_de_vs, HDMI_FC_VSYNCINDELAY);

	/* Set up HSYNC active pulse width (in pixel clks) */
	hdmi_writeb(hdmi, hsync_len >> 8, HDMI_FC_HSYNCINWIDTH1);
	hdmi_writeb(hdmi, hsync_len, HDMI_FC_HSYNCINWIDTH0);

	/* Set up VSYNC active edge delay (in lines) */
	hdmi_writeb(hdmi, vsync_len, HDMI_FC_VSYNCINWIDTH);
}

static void dw_hdmi_update_csc_coeffs(struct dw_hdmi *hdmi)
{
	const u16 (*csc_coeff)[3][4] = &csc_coeff_default;
	unsigned i;
	u32 csc_scale = 1;

	if (is_color_space_conversion(hdmi)) {
		if (hdmi_bus_fmt_is_rgb(hdmi->hdmi_data.enc_out_bus_format)) {
			if (hdmi->hdmi_data.enc_out_encoding ==
						V4L2_YCBCR_ENC_601)
				csc_coeff = &csc_coeff_rgb_out_eitu601;
			else
				csc_coeff = &csc_coeff_rgb_out_eitu709;
		} else if (hdmi_bus_fmt_is_rgb(
					hdmi->hdmi_data.enc_in_bus_format)) {
			if (hdmi->hdmi_data.enc_out_encoding ==
						V4L2_YCBCR_ENC_601)
				csc_coeff = &csc_coeff_rgb_in_eitu601;
			else
				csc_coeff = &csc_coeff_rgb_in_eitu709;
			csc_scale = 0;
		}
	}

	/* The CSC registers are sequential, alternating MSB then LSB */
	for (i = 0; i < ARRAY_SIZE(csc_coeff_default[0]); i++) {
		u16 coeff_a = (*csc_coeff)[0][i];
		u16 coeff_b = (*csc_coeff)[1][i];
		u16 coeff_c = (*csc_coeff)[2][i];

		hdmi_writeb(hdmi, coeff_a & 0xff, HDMI_CSC_COEF_A1_LSB + i * 2);
		hdmi_writeb(hdmi, coeff_a >> 8, HDMI_CSC_COEF_A1_MSB + i * 2);
		hdmi_writeb(hdmi, coeff_b & 0xff, HDMI_CSC_COEF_B1_LSB + i * 2);
		hdmi_writeb(hdmi, coeff_b >> 8, HDMI_CSC_COEF_B1_MSB + i * 2);
		hdmi_writeb(hdmi, coeff_c & 0xff, HDMI_CSC_COEF_C1_LSB + i * 2);
		hdmi_writeb(hdmi, coeff_c >> 8, HDMI_CSC_COEF_C1_MSB + i * 2);
	}

	hdmi_modb(hdmi, csc_scale, HDMI_CSC_SCALE_CSCSCALE_MASK,
		  HDMI_CSC_SCALE);
}

static int is_color_space_interpolation(struct dw_hdmi *hdmi)
{
	if (!hdmi_bus_fmt_is_yuv422(hdmi->hdmi_data.enc_in_bus_format))
		return 0;

	if (hdmi_bus_fmt_is_rgb(hdmi->hdmi_data.enc_out_bus_format) ||
	    hdmi_bus_fmt_is_yuv444(hdmi->hdmi_data.enc_out_bus_format))
		return 1;

	return 0;
}

static void hdmi_video_csc(struct dw_hdmi *hdmi)
{
	int color_depth = 0;
	int interpolation = HDMI_CSC_CFG_INTMODE_DISABLE;
	int decimation = 0;

	/* YCC422 interpolation to 444 mode */
	if (is_color_space_interpolation(hdmi))
		interpolation = HDMI_CSC_CFG_INTMODE_CHROMA_INT_FORMULA1;
	else if (is_color_space_decimation(hdmi))
		decimation = HDMI_CSC_CFG_DECMODE_CHROMA_INT_FORMULA3;

	switch (hdmi_bus_fmt_color_depth(hdmi->hdmi_data.enc_out_bus_format)) {
	case 8:
		color_depth = HDMI_CSC_SCALE_CSC_COLORDE_PTH_24BPP;
		break;
	case 10:
		color_depth = HDMI_CSC_SCALE_CSC_COLORDE_PTH_30BPP;
		break;
	case 12:
		color_depth = HDMI_CSC_SCALE_CSC_COLORDE_PTH_36BPP;
		break;
	case 16:
		color_depth = HDMI_CSC_SCALE_CSC_COLORDE_PTH_48BPP;
		break;

	default:
		return;
	}

	/* Configure the CSC registers */
	hdmi_writeb(hdmi, interpolation | decimation, HDMI_CSC_CFG);
	hdmi_modb(hdmi, color_depth, HDMI_CSC_SCALE_CSC_COLORDE_PTH_MASK,
		  HDMI_CSC_SCALE);

	dw_hdmi_update_csc_coeffs(hdmi);
}

static void dw_hdmi_enable_video_path(struct dw_hdmi *hdmi)
{
	u8 clkdis;

	/* control period minimum duration */
	hdmi_writeb(hdmi, 12, HDMI_FC_CTRLDUR);
	hdmi_writeb(hdmi, 32, HDMI_FC_EXCTRLDUR);
	hdmi_writeb(hdmi, 1, HDMI_FC_EXCTRLSPAC);

	/* Set to fill TMDS data channels */
	hdmi_writeb(hdmi, 0x0B, HDMI_FC_CH0PREAM);
	hdmi_writeb(hdmi, 0x16, HDMI_FC_CH1PREAM);
	hdmi_writeb(hdmi, 0x21, HDMI_FC_CH2PREAM);

	/* Enable pixel clock and tmds data path */
	clkdis = 0x7F;
	clkdis &= ~HDMI_MC_CLKDIS_PIXELCLK_DISABLE;
	hdmi_writeb(hdmi, clkdis, HDMI_MC_CLKDIS);

	clkdis &= ~HDMI_MC_CLKDIS_TMDSCLK_DISABLE;
	hdmi_writeb(hdmi, clkdis, HDMI_MC_CLKDIS);

	/* Enable csc path */
	if (is_color_space_conversion(hdmi)) {
		clkdis &= ~HDMI_MC_CLKDIS_CSCCLK_DISABLE;
		hdmi_writeb(hdmi, clkdis, HDMI_MC_CLKDIS);
	}

	/* Enable pixel repetition path */
	if (hdmi->hdmi_data.video_mode.mpixelrepetitioninput) {
		clkdis &= ~HDMI_MC_CLKDIS_PREPCLK_DISABLE;
		hdmi_writeb(hdmi, clkdis, HDMI_MC_CLKDIS);
	}

	/* Enable color space conversion if needed */
	if (is_color_space_conversion(hdmi))
		hdmi_writeb(hdmi, HDMI_MC_FLOWCTRL_FEED_THROUGH_OFF_CSC_IN_PATH,
			    HDMI_MC_FLOWCTRL);
	else
		hdmi_writeb(hdmi, HDMI_MC_FLOWCTRL_FEED_THROUGH_OFF_CSC_BYPASS,
			    HDMI_MC_FLOWCTRL);
}

static void dw_hdmi_clear_overflow(struct dw_hdmi *hdmi)
{
	unsigned int count;
	unsigned int i;
	u8 val;

	/*
	 * Under some circumstances the Frame Composer arithmetic unit can miss
	 * an FC register write due to being busy processing the previous one.
	 * The issue can be worked around by issuing a TMDS software reset and
	 * then write one of the FC registers several times.
	 *
	 * The number of iterations matters and depends on the HDMI TX revision
	 * (and possibly on the platform). So far only i.MX6Q (v1.30a) and
	 * i.MX6DL (v1.31a) have been identified as needing the workaround, with
	 * 4 and 1 iterations respectively.
	 */

	switch (hdmi->version) {
	case 0x130a:
		count = 4;
		break;
	case 0x131a:
		count = 1;
		break;
	default:
		return;
	}

	/* TMDS software reset */
	hdmi_writeb(hdmi, (u8)~HDMI_MC_SWRSTZ_TMDSSWRST_REQ, HDMI_MC_SWRSTZ);

	val = hdmi_readb(hdmi, HDMI_FC_INVIDCONF);
	for (i = 0; i < count; i++)
		hdmi_writeb(hdmi, val, HDMI_FC_INVIDCONF);
}

static void hdmi_disable_overflow_interrupts(struct dw_hdmi *hdmi)
{
	hdmi_writeb(hdmi, HDMI_IH_MUTE_FC_STAT2_OVERFLOW_MASK,
		    HDMI_IH_MUTE_FC_STAT2);
}

static void hdmi_video_packetize(struct dw_hdmi *hdmi)
{
	unsigned int color_depth = 0;
	unsigned int remap_size = HDMI_VP_REMAP_YCC422_16bit;
	unsigned int output_select = HDMI_VP_CONF_OUTPUT_SELECTOR_PP;
	struct hdmi_data_info *hdmi_data = &hdmi->hdmi_data;
	u8 val, vp_conf;

	if (hdmi_bus_fmt_is_rgb(hdmi->hdmi_data.enc_out_bus_format) ||
	    hdmi_bus_fmt_is_yuv444(hdmi->hdmi_data.enc_out_bus_format) ||
	    hdmi_bus_fmt_is_yuv420(hdmi->hdmi_data.enc_out_bus_format)) {
		switch (hdmi_bus_fmt_color_depth(
					hdmi->hdmi_data.enc_out_bus_format)) {
		case 8:
			color_depth = 0;
			output_select = HDMI_VP_CONF_OUTPUT_SELECTOR_BYPASS;
			break;
		case 10:
			color_depth = 5;
			break;
		case 12:
			color_depth = 6;
			break;
		case 16:
			color_depth = 7;
			break;
		default:
			output_select = HDMI_VP_CONF_OUTPUT_SELECTOR_BYPASS;
		}
	} else if (hdmi_bus_fmt_is_yuv422(hdmi->hdmi_data.enc_out_bus_format)) {
		switch (hdmi_bus_fmt_color_depth(
					hdmi->hdmi_data.enc_out_bus_format)) {
		case 0:
		case 8:
			remap_size = HDMI_VP_REMAP_YCC422_16bit;
			break;
		case 10:
			remap_size = HDMI_VP_REMAP_YCC422_20bit;
			break;
		case 12:
			remap_size = HDMI_VP_REMAP_YCC422_24bit;
			break;

		default:
			return;
		}
		output_select = HDMI_VP_CONF_OUTPUT_SELECTOR_YCC422;
	} else {
		return;
	}

	/* set the packetizer registers */
	val = ((color_depth << HDMI_VP_PR_CD_COLOR_DEPTH_OFFSET) &
		HDMI_VP_PR_CD_COLOR_DEPTH_MASK) |
		((hdmi_data->pix_repet_factor <<
		HDMI_VP_PR_CD_DESIRED_PR_FACTOR_OFFSET) &
		HDMI_VP_PR_CD_DESIRED_PR_FACTOR_MASK);
	hdmi_writeb(hdmi, val, HDMI_VP_PR_CD);

	hdmi_modb(hdmi, HDMI_VP_STUFF_PR_STUFFING_STUFFING_MODE,
		  HDMI_VP_STUFF_PR_STUFFING_MASK, HDMI_VP_STUFF);

	/* Data from pixel repeater block */
	if (hdmi_data->pix_repet_factor > 0) {
		vp_conf = HDMI_VP_CONF_PR_EN_ENABLE |
			  HDMI_VP_CONF_BYPASS_SELECT_PIX_REPEATER;
	} else { /* data from packetizer block */
		vp_conf = HDMI_VP_CONF_PR_EN_DISABLE |
			  HDMI_VP_CONF_BYPASS_SELECT_VID_PACKETIZER;
	}

	hdmi_modb(hdmi, vp_conf,
		  HDMI_VP_CONF_PR_EN_MASK |
		  HDMI_VP_CONF_BYPASS_SELECT_MASK, HDMI_VP_CONF);

	hdmi_modb(hdmi, 1 << HDMI_VP_STUFF_IDEFAULT_PHASE_OFFSET,
		  HDMI_VP_STUFF_IDEFAULT_PHASE_MASK, HDMI_VP_STUFF);

	hdmi_writeb(hdmi, remap_size, HDMI_VP_REMAP);

	if (output_select == HDMI_VP_CONF_OUTPUT_SELECTOR_PP) {
		vp_conf = HDMI_VP_CONF_BYPASS_EN_DISABLE |
			  HDMI_VP_CONF_PP_EN_ENABLE |
			  HDMI_VP_CONF_YCC422_EN_DISABLE;
	} else if (output_select == HDMI_VP_CONF_OUTPUT_SELECTOR_YCC422) {
		vp_conf = HDMI_VP_CONF_BYPASS_EN_DISABLE |
			  HDMI_VP_CONF_PP_EN_DISABLE |
			  HDMI_VP_CONF_YCC422_EN_ENABLE;
	} else if (output_select == HDMI_VP_CONF_OUTPUT_SELECTOR_BYPASS) {
		vp_conf = HDMI_VP_CONF_BYPASS_EN_ENABLE |
			  HDMI_VP_CONF_PP_EN_DISABLE |
			  HDMI_VP_CONF_YCC422_EN_DISABLE;
	} else {
		return;
	}

	hdmi_modb(hdmi, vp_conf,
		  HDMI_VP_CONF_BYPASS_EN_MASK | HDMI_VP_CONF_PP_EN_ENMASK |
		  HDMI_VP_CONF_YCC422_EN_MASK, HDMI_VP_CONF);

	hdmi_modb(hdmi, HDMI_VP_STUFF_PP_STUFFING_STUFFING_MODE |
			HDMI_VP_STUFF_YCC422_STUFFING_STUFFING_MODE,
		  HDMI_VP_STUFF_PP_STUFFING_MASK |
		  HDMI_VP_STUFF_YCC422_STUFFING_MASK, HDMI_VP_STUFF);

	hdmi_modb(hdmi, output_select, HDMI_VP_CONF_OUTPUT_SELECTOR_MASK,
		  HDMI_VP_CONF);
}

static void hdmi_video_sample(struct dw_hdmi *hdmi)
{
	int color_format = 0;
	u8 val;

	switch (hdmi->hdmi_data.enc_in_bus_format) {
	case MEDIA_BUS_FMT_RGB888_1X24:
		color_format = 0x01;
		break;
	case MEDIA_BUS_FMT_RGB101010_1X30:
		color_format = 0x03;
		break;
	case MEDIA_BUS_FMT_RGB121212_1X36:
		color_format = 0x05;
		break;
	case MEDIA_BUS_FMT_RGB161616_1X48:
		color_format = 0x07;
		break;

	case MEDIA_BUS_FMT_YUV8_1X24:
	case MEDIA_BUS_FMT_UYYVYY8_0_5X24:
		color_format = 0x09;
		break;
	case MEDIA_BUS_FMT_YUV10_1X30:
	case MEDIA_BUS_FMT_UYYVYY10_0_5X30:
		color_format = 0x0B;
		break;
	case MEDIA_BUS_FMT_YUV12_1X36:
	case MEDIA_BUS_FMT_UYYVYY12_0_5X36:
		color_format = 0x0D;
		break;
	case MEDIA_BUS_FMT_YUV16_1X48:
	case MEDIA_BUS_FMT_UYYVYY16_0_5X48:
		color_format = 0x0F;
		break;

	case MEDIA_BUS_FMT_UYVY8_1X16:
		color_format = 0x16;
		break;
	case MEDIA_BUS_FMT_UYVY10_1X20:
		color_format = 0x14;
		break;
	case MEDIA_BUS_FMT_UYVY12_1X24:
		color_format = 0x12;
		break;

	default:
		return;
	}

	val = HDMI_TX_INVID0_INTERNAL_DE_GENERATOR_DISABLE |
		((color_format << HDMI_TX_INVID0_VIDEO_MAPPING_OFFSET) &
		HDMI_TX_INVID0_VIDEO_MAPPING_MASK);
	hdmi_writeb(hdmi, val, HDMI_TX_INVID0);

	/* Enable TX stuffing: When DE is inactive, fix the output data to 0 */
	val = HDMI_TX_INSTUFFING_BDBDATA_STUFFING_ENABLE |
		HDMI_TX_INSTUFFING_RCRDATA_STUFFING_ENABLE |
		HDMI_TX_INSTUFFING_GYDATA_STUFFING_ENABLE;
	hdmi_writeb(hdmi, val, HDMI_TX_INSTUFFING);
	hdmi_writeb(hdmi, 0x0, HDMI_TX_GYDATA0);
	hdmi_writeb(hdmi, 0x0, HDMI_TX_GYDATA1);
	hdmi_writeb(hdmi, 0x0, HDMI_TX_RCRDATA0);
	hdmi_writeb(hdmi, 0x0, HDMI_TX_RCRDATA1);
	hdmi_writeb(hdmi, 0x0, HDMI_TX_BCBDATA0);
	hdmi_writeb(hdmi, 0x0, HDMI_TX_BCBDATA1);
}

static void hdmi_enable_overflow_interrupts(struct dw_hdmi *hdmi)
{
	hdmi_writeb(hdmi, 0, HDMI_FC_MASK2);
	hdmi_writeb(hdmi, 0, HDMI_IH_MUTE_FC_STAT2);
}

static void dw_hdmi_disable(struct dw_hdmi *hdmi)
{
	if (hdmi->phy.enabled) {
		hdmi->phy.ops->disable(hdmi, hdmi->phy.data);
		hdmi->phy.enabled = false;
	}
}

static void hdmi_config_AVI(struct dw_hdmi *hdmi, struct drm_display_mode *mode)
{
	struct hdmi_avi_infoframe frame;
	u8 val;
	bool is_hdmi2 = false;

	if (hdmi_bus_fmt_is_yuv420(hdmi->hdmi_data.enc_out_bus_format) ||
	    hdmi->edid_data.display_info.hdmi.scdc.supported)
		is_hdmi2 = true;
	/* Initialise info frame from DRM mode */
	drm_hdmi_avi_infoframe_from_display_mode(&frame, mode, is_hdmi2);

	if (hdmi_bus_fmt_is_yuv444(hdmi->hdmi_data.enc_out_bus_format))
		frame.colorspace = HDMI_COLORSPACE_YUV444;
	else if (hdmi_bus_fmt_is_yuv422(hdmi->hdmi_data.enc_out_bus_format))
		frame.colorspace = HDMI_COLORSPACE_YUV422;
	else if (hdmi_bus_fmt_is_yuv420(hdmi->hdmi_data.enc_out_bus_format))
		frame.colorspace = HDMI_COLORSPACE_YUV420;
	else
		frame.colorspace = HDMI_COLORSPACE_RGB;

	/* Set up colorimetry */
	switch (hdmi->hdmi_data.enc_out_encoding) {
	case V4L2_YCBCR_ENC_601:
		if (hdmi->hdmi_data.enc_in_encoding == V4L2_YCBCR_ENC_XV601)
			frame.colorimetry = HDMI_COLORIMETRY_EXTENDED;
		else
			frame.colorimetry = HDMI_COLORIMETRY_ITU_601;
		frame.extended_colorimetry =
				HDMI_EXTENDED_COLORIMETRY_XV_YCC_601;
		break;
	case V4L2_YCBCR_ENC_709:
		if (hdmi->hdmi_data.enc_in_encoding == V4L2_YCBCR_ENC_XV709)
			frame.colorimetry = HDMI_COLORIMETRY_EXTENDED;
		else
			frame.colorimetry = HDMI_COLORIMETRY_ITU_709;
		frame.extended_colorimetry =
				HDMI_EXTENDED_COLORIMETRY_XV_YCC_709;
		break;
	default: /* Carries no data */
		frame.colorimetry = HDMI_COLORIMETRY_ITU_601;
		frame.extended_colorimetry =
				HDMI_EXTENDED_COLORIMETRY_XV_YCC_601;
		break;
	}

	frame.scan_mode = HDMI_SCAN_MODE_NONE;

	/*
	 * The Designware IP uses a different byte format from standard
	 * AVI info frames, though generally the bits are in the correct
	 * bytes.
	 */

	/*
	 * AVI data byte 1 differences: Colorspace in bits 0,1,7 rather than
	 * 5,6,7, active aspect present in bit 6 rather than 4.
	 */
	val = (frame.scan_mode & 3) << 4 | (frame.colorspace & 0x3);
	if (frame.active_aspect & 15)
		val |= HDMI_FC_AVICONF0_ACTIVE_FMT_INFO_PRESENT;
	if (frame.top_bar || frame.bottom_bar)
		val |= HDMI_FC_AVICONF0_BAR_DATA_HORIZ_BAR;
	if (frame.left_bar || frame.right_bar)
		val |= HDMI_FC_AVICONF0_BAR_DATA_VERT_BAR;
	hdmi_writeb(hdmi, val, HDMI_FC_AVICONF0);

	/* AVI data byte 2 differences: none */
	val = ((frame.colorimetry & 0x3) << 6) |
	      ((frame.picture_aspect & 0x3) << 4) |
	      (frame.active_aspect & 0xf);
	hdmi_writeb(hdmi, val, HDMI_FC_AVICONF1);

	/* AVI data byte 3 differences: none */
	val = ((frame.extended_colorimetry & 0x7) << 4) |
	      ((frame.quantization_range & 0x3) << 2) |
	      (frame.nups & 0x3);
	if (frame.itc)
		val |= HDMI_FC_AVICONF2_IT_CONTENT_VALID;
	hdmi_writeb(hdmi, val, HDMI_FC_AVICONF2);

	/* AVI data byte 4 differences: none */
	val = frame.video_code & 0x7f;
	hdmi_writeb(hdmi, val, HDMI_FC_AVIVID);

	/* AVI Data Byte 5- set up input and output pixel repetition */
	val = (((hdmi->hdmi_data.video_mode.mpixelrepetitioninput + 1) <<
		HDMI_FC_PRCONF_INCOMING_PR_FACTOR_OFFSET) &
		HDMI_FC_PRCONF_INCOMING_PR_FACTOR_MASK) |
		((hdmi->hdmi_data.video_mode.mpixelrepetitionoutput <<
		HDMI_FC_PRCONF_OUTPUT_PR_FACTOR_OFFSET) &
		HDMI_FC_PRCONF_OUTPUT_PR_FACTOR_MASK);
	hdmi_writeb(hdmi, val, HDMI_FC_PRCONF);

	/*
	 * AVI data byte 5 differences: content type in 0,1 rather than 4,5,
	 * ycc range in bits 2,3 rather than 6,7
	 */
	val = ((frame.ycc_quantization_range & 0x3) << 2) |
	      (frame.content_type & 0x3);
	hdmi_writeb(hdmi, val, HDMI_FC_AVICONF3);

	/* AVI Data Bytes 6-13 */
	hdmi_writeb(hdmi, frame.top_bar & 0xff, HDMI_FC_AVIETB0);
	hdmi_writeb(hdmi, (frame.top_bar >> 8) & 0xff, HDMI_FC_AVIETB1);
	hdmi_writeb(hdmi, frame.bottom_bar & 0xff, HDMI_FC_AVISBB0);
	hdmi_writeb(hdmi, (frame.bottom_bar >> 8) & 0xff, HDMI_FC_AVISBB1);
	hdmi_writeb(hdmi, frame.left_bar & 0xff, HDMI_FC_AVIELB0);
	hdmi_writeb(hdmi, (frame.left_bar >> 8) & 0xff, HDMI_FC_AVIELB1);
	hdmi_writeb(hdmi, frame.right_bar & 0xff, HDMI_FC_AVISRB0);
	hdmi_writeb(hdmi, (frame.right_bar >> 8) & 0xff, HDMI_FC_AVISRB1);
}

static void hdmi_config_vendor_specific_infoframe(struct dw_hdmi *hdmi,
						  struct drm_display_mode *mode)
{
	struct hdmi_vendor_infoframe frame;
	u8 buffer[10];
	ssize_t err;

	/* Disable HDMI vendor specific infoframe send */
	hdmi_mask_writeb(hdmi, 0, HDMI_FC_DATAUTO0, HDMI_FC_DATAUTO0_VSD_OFFSET,
			 HDMI_FC_DATAUTO0_VSD_MASK);

	err = drm_hdmi_vendor_infoframe_from_display_mode(&frame, mode);
	if (err < 0)
		/*
		 * Going into that statement does not means vendor infoframe
		 * fails. It just informed us that vendor infoframe is not
		 * needed for the selected mode. Only 4k or stereoscopic 3D
		 * mode requires vendor infoframe. So just simply return.
		 */
		return;

	err = hdmi_vendor_infoframe_pack(&frame, buffer, sizeof(buffer));
	if (err < 0) {
		printf("Failed to pack vendor infoframe: %zd\n", err);
		return;
	}

	/* Set the length of HDMI vendor specific InfoFrame payload */
	hdmi_writeb(hdmi, buffer[2], HDMI_FC_VSDSIZE);

	/* Set 24bit IEEE Registration Identifier */
	hdmi_writeb(hdmi, buffer[4], HDMI_FC_VSDIEEEID0);
	hdmi_writeb(hdmi, buffer[5], HDMI_FC_VSDIEEEID1);
	hdmi_writeb(hdmi, buffer[6], HDMI_FC_VSDIEEEID2);

	/* Set HDMI_Video_Format and HDMI_VIC/3D_Structure */
	hdmi_writeb(hdmi, buffer[7], HDMI_FC_VSDPAYLOAD0);
	hdmi_writeb(hdmi, buffer[8], HDMI_FC_VSDPAYLOAD1);

	if (frame.s3d_struct >= HDMI_3D_STRUCTURE_SIDE_BY_SIDE_HALF)
		hdmi_writeb(hdmi, buffer[9], HDMI_FC_VSDPAYLOAD2);

	/* Packet frame interpolation */
	hdmi_writeb(hdmi, 1, HDMI_FC_DATAUTO1);

	/* Auto packets per frame and line spacing */
	hdmi_writeb(hdmi, 0x11, HDMI_FC_DATAUTO2);

	/* Configures the Frame Composer On RDRB mode */
	hdmi_mask_writeb(hdmi, 1, HDMI_FC_DATAUTO0, HDMI_FC_DATAUTO0_VSD_OFFSET,
			 HDMI_FC_DATAUTO0_VSD_MASK);
}

static void hdmi_set_cts_n(struct dw_hdmi *hdmi, unsigned int cts,
			   unsigned int n)
{
	/* Must be set/cleared first */
	hdmi_modb(hdmi, 0, HDMI_AUD_CTS3_CTS_MANUAL, HDMI_AUD_CTS3);

	/* nshift factor = 0 */
	hdmi_modb(hdmi, 0, HDMI_AUD_CTS3_N_SHIFT_MASK, HDMI_AUD_CTS3);

	hdmi_writeb(hdmi, ((cts >> 16) & HDMI_AUD_CTS3_AUDCTS19_16_MASK) |
		    HDMI_AUD_CTS3_CTS_MANUAL, HDMI_AUD_CTS3);
	hdmi_writeb(hdmi, (cts >> 8) & 0xff, HDMI_AUD_CTS2);
	hdmi_writeb(hdmi, cts & 0xff, HDMI_AUD_CTS1);

	hdmi_writeb(hdmi, (n >> 16) & 0x0f, HDMI_AUD_N3);
	hdmi_writeb(hdmi, (n >> 8) & 0xff, HDMI_AUD_N2);
	hdmi_writeb(hdmi, n & 0xff, HDMI_AUD_N1);
}

static int hdmi_match_tmds_n_table(struct dw_hdmi *hdmi,
				   unsigned long pixel_clk,
				   unsigned long freq)
{
	const struct dw_hdmi_plat_data *plat_data = hdmi->plat_data;
	const struct dw_hdmi_audio_tmds_n *tmds_n = NULL;
	int i;

	if (plat_data->tmds_n_table) {
		for (i = 0; plat_data->tmds_n_table[i].tmds != 0; i++) {
			if (pixel_clk == plat_data->tmds_n_table[i].tmds) {
				tmds_n = &plat_data->tmds_n_table[i];
				break;
			}
		}
	}

	if (!tmds_n) {
		for (i = 0; common_tmds_n_table[i].tmds != 0; i++) {
			if (pixel_clk == common_tmds_n_table[i].tmds) {
				tmds_n = &common_tmds_n_table[i];
				break;
			}
		}
	}

	if (!tmds_n)
		return -ENOENT;

	switch (freq) {
	case 32000:
		return tmds_n->n_32k;
	case 44100:
	case 88200:
	case 176400:
		return (freq / 44100) * tmds_n->n_44k1;
	case 48000:
	case 96000:
	case 192000:
		return (freq / 48000) * tmds_n->n_48k;
	default:
		return -ENOENT;
	}
}

static u64 hdmi_audio_math_diff(unsigned int freq, unsigned int n,
				unsigned int pixel_clk)
{
	u64 final, diff;
	u64 cts;

	final = (u64)pixel_clk * n;

	cts = final;
	do_div(cts, 128 * freq);

	diff = final - (u64)cts * (128 * freq);

	return diff;
}

static unsigned int hdmi_compute_n(struct dw_hdmi *hdmi,
				   unsigned long pixel_clk,
				   unsigned long freq)
{
	unsigned int min_n = DIV_ROUND_UP((128 * freq), 1500);
	unsigned int max_n = (128 * freq) / 300;
	unsigned int ideal_n = (128 * freq) / 1000;
	unsigned int best_n_distance = ideal_n;
	unsigned int best_n = 0;
	u64 best_diff = U64_MAX;
	int n;

	/* If the ideal N could satisfy the audio math, then just take it */
	if (hdmi_audio_math_diff(freq, ideal_n, pixel_clk) == 0)
		return ideal_n;

	for (n = min_n; n <= max_n; n++) {
		u64 diff = hdmi_audio_math_diff(freq, n, pixel_clk);

		if (diff < best_diff || (diff == best_diff &&
					 abs(n - ideal_n) < best_n_distance)) {
			best_n = n;
			best_diff = diff;
			best_n_distance = abs(best_n - ideal_n);
		}

		/*
		 * The best N already satisfy the audio math, and also be
		 * the closest value to ideal N, so just cut the loop.
		 */
		if ((best_diff == 0) && (abs(n - ideal_n) > best_n_distance))
			break;
	}

	return best_n;
}

static unsigned int hdmi_find_n(struct dw_hdmi *hdmi, unsigned long pixel_clk,
				unsigned long sample_rate)
{
	int n;

	n = hdmi_match_tmds_n_table(hdmi, pixel_clk, sample_rate);
	if (n > 0)
		return n;

	printf("Rate %lu missing; compute N dynamically\n",
	       pixel_clk);

	return hdmi_compute_n(hdmi, pixel_clk, sample_rate);
}

static
void hdmi_set_clk_regenerator(struct dw_hdmi *hdmi, unsigned long pixel_clk,
			      unsigned int sample_rate)
{
	unsigned long ftdms = pixel_clk;
	unsigned int n, cts;
	u64 tmp;

	n = hdmi_find_n(hdmi, pixel_clk, sample_rate);

	/*
	 * Compute the CTS value from the N value.  Note that CTS and N
	 * can be up to 20 bits in total, so we need 64-bit math.  Also
	 * note that our TDMS clock is not fully accurate; it is accurate
	 * to kHz.  This can introduce an unnecessary remainder in the
	 * calculation below, so we don't try to warn about that.
	 */
	tmp = (u64)ftdms * n;
	do_div(tmp, 128 * sample_rate);
	cts = tmp;

	printf("%s: fs=%uHz ftdms=%lu.%03luMHz N=%d cts=%d\n", __func__,
	       sample_rate, ftdms / 1000000, (ftdms / 1000) % 1000, n, cts);

	hdmi->audio_n = n;
	hdmi->audio_cts = cts;
	hdmi_set_cts_n(hdmi, cts, hdmi->audio_enable ? n : 0);
}

static void hdmi_clk_regenerator_update_pixel_clock(struct dw_hdmi *hdmi)
{
	hdmi_set_clk_regenerator(hdmi, hdmi->hdmi_data.video_mode.mpixelclock,
				 hdmi->sample_rate);
}

static void hdmi_enable_audio_clk(struct dw_hdmi *hdmi)
{
	hdmi_modb(hdmi, 0, HDMI_MC_CLKDIS_AUDCLK_DISABLE, HDMI_MC_CLKDIS);
}

void dw_hdmi_set_sample_rate(struct dw_hdmi *hdmi, unsigned int rate)
{
	hdmi->sample_rate = rate;
	hdmi_set_clk_regenerator(hdmi, hdmi->hdmi_data.video_mode.mpixelclock,
				 hdmi->sample_rate);
}

static int dw_hdmi_setup(struct dw_hdmi *hdmi,
			 struct drm_display_mode *mode)
{
	int ret;
	void *data = hdmi->plat_data->phy_data;

	hdmi_disable_overflow_interrupts(hdmi);

	if (!hdmi->vic)
		printf("Non-CEA mode used in HDMI\n");
	else
		printf("CEA mode used vic=%d\n", hdmi->vic);

	if (hdmi->plat_data->get_enc_out_encoding)
		hdmi->hdmi_data.enc_out_encoding =
			hdmi->plat_data->get_enc_out_encoding(data);
	else if ((hdmi->vic == 6) || (hdmi->vic == 7) ||
		 (hdmi->vic == 21) || (hdmi->vic == 22) ||
		 (hdmi->vic == 2) || (hdmi->vic == 3) ||
		 (hdmi->vic == 17) || (hdmi->vic == 18))
		hdmi->hdmi_data.enc_out_encoding = V4L2_YCBCR_ENC_601;
	else
		hdmi->hdmi_data.enc_out_encoding = V4L2_YCBCR_ENC_709;

	if (mode->flags & DRM_MODE_FLAG_DBLCLK) {
		hdmi->hdmi_data.video_mode.mpixelrepetitionoutput = 1;
		hdmi->hdmi_data.video_mode.mpixelrepetitioninput = 1;
	} else {
		hdmi->hdmi_data.video_mode.mpixelrepetitionoutput = 0;
		hdmi->hdmi_data.video_mode.mpixelrepetitioninput = 0;
	}

	/* TOFIX: Get input format from plat data or fallback to RGB888 */
	if (hdmi->plat_data->get_input_bus_format)
		hdmi->hdmi_data.enc_in_bus_format =
			hdmi->plat_data->get_input_bus_format(data);
	else if (hdmi->plat_data->input_bus_format)
		hdmi->hdmi_data.enc_in_bus_format =
			hdmi->plat_data->input_bus_format;
	else
		hdmi->hdmi_data.enc_in_bus_format =
			MEDIA_BUS_FMT_RGB888_1X24;

	/* TOFIX: Default to RGB888 output format */
	if (hdmi->plat_data->get_output_bus_format)
		hdmi->hdmi_data.enc_out_bus_format =
			hdmi->plat_data->get_output_bus_format(data);
	else
		hdmi->hdmi_data.enc_out_bus_format =
			MEDIA_BUS_FMT_RGB888_1X24;

	/* TOFIX: Get input encoding from plat data or fallback to none */
	if (hdmi->plat_data->get_enc_in_encoding)
		hdmi->hdmi_data.enc_in_encoding =
			hdmi->plat_data->get_enc_in_encoding(data);
	else if (hdmi->plat_data->input_bus_encoding)
		hdmi->hdmi_data.enc_in_encoding =
			hdmi->plat_data->input_bus_encoding;
	else
		hdmi->hdmi_data.enc_in_encoding = V4L2_YCBCR_ENC_DEFAULT;
	/*
	 * According to the dw-hdmi specification 6.4.2
	 * vp_pr_cd[3:0]:
	 * 0000b: No pixel repetition (pixel sent only once)
	 * 0001b: Pixel sent two times (pixel repeated once)
	 */
	hdmi->hdmi_data.pix_repet_factor =
		(mode->flags & DRM_MODE_FLAG_DBLCLK) ? 1 : 0;
	hdmi->hdmi_data.video_mode.mdataenablepolarity = true;

	/* HDMI Initialization Step B.1 */
	hdmi_av_composer(hdmi, mode);

	/* HDMI Initialization Step B.2 */
	ret = hdmi->phy.ops->init(hdmi, hdmi->phy.data, &hdmi->previous_mode);
	if (ret)
		return ret;
	hdmi->phy.enabled = true;

	/* HDMI Initializateion Step B.3 */
	dw_hdmi_enable_video_path(hdmi);

	/* HDMI Initialization Step E - Configure audio */
	if (hdmi->sink_has_audio) {
		printf("sink has audio support\n");
		hdmi_clk_regenerator_update_pixel_clock(hdmi);
		hdmi_enable_audio_clk(hdmi);
	}

	/* not for DVI mode */
	if (hdmi->sink_is_hdmi) {
		printf("%s HDMI mode\n", __func__);

		/* HDMI Initialization Step F - Configure AVI InfoFrame */
		hdmi_config_AVI(hdmi, mode);
		hdmi_config_vendor_specific_infoframe(hdmi, mode);
	} else {
		printf("%s DVI mode\n", __func__);
	}

	hdmi_video_packetize(hdmi);
	hdmi_video_csc(hdmi);
	hdmi_video_sample(hdmi);
	dw_hdmi_clear_overflow(hdmi);
	if (hdmi->cable_plugin && hdmi->sink_is_hdmi)
		hdmi_enable_overflow_interrupts(hdmi);

	return 0;
}

int dw_hdmi_detect_hotplug(struct dw_hdmi *hdmi)
{
	return hdmi->phy.ops->read_hpd(hdmi, hdmi->phy.data);
}

static int dw_hdmi_set_reg_wr(struct dw_hdmi *hdmi)
{
	switch (hdmi->io_width) {
	case 4:
		hdmi->write = dw_hdmi_writel;
		hdmi->read = dw_hdmi_readl;
		break;
	case 1:
		hdmi->write = dw_hdmi_writeb;
		hdmi->read = dw_hdmi_readb;
		break;
	default:
		printf("reg-io-width must be 1 or 4\n");
		return -EINVAL;
	}

	return 0;
}

static void initialize_hdmi_mutes(struct dw_hdmi *hdmi)
{
	/*mute unnecessary interrupt, only enable hpd */
	hdmi_writeb(hdmi, 0xff, HDMI_IH_MUTE_FC_STAT0);
	hdmi_writeb(hdmi, 0xff, HDMI_IH_MUTE_FC_STAT1);
	hdmi_writeb(hdmi, 0xff, HDMI_IH_MUTE_FC_STAT2);
	hdmi_writeb(hdmi, 0xff, HDMI_IH_MUTE_AS_STAT0);
	hdmi_writeb(hdmi, 0xfe, HDMI_IH_MUTE_PHY_STAT0);
	hdmi_writeb(hdmi, 0xff, HDMI_IH_MUTE_I2CM_STAT0);
	hdmi_writeb(hdmi, 0xff, HDMI_IH_MUTE_CEC_STAT0);
	hdmi_writeb(hdmi, 0xff, HDMI_IH_MUTE_VP_STAT0);
	hdmi_writeb(hdmi, 0xff, HDMI_IH_MUTE_I2CMPHY_STAT0);
	hdmi_writeb(hdmi, 0xff, HDMI_IH_MUTE_AHBDMAAUD_STAT0);
	hdmi_writeb(hdmi, 0xf1, HDMI_PHY_MASK0);

	/*Force output black*/
	dw_hdmi_writel(hdmi, 0x00, HDMI_FC_DBGTMDS2);
	dw_hdmi_writel(hdmi, 0x00, HDMI_FC_DBGTMDS1);
	dw_hdmi_writel(hdmi, 0x00, HDMI_FC_DBGTMDS0);
}

static void dw_hdmi_dev_init(struct dw_hdmi *hdmi)
{
	hdmi->version = (hdmi_readb(hdmi, HDMI_DESIGN_ID) << 8)
		      | (hdmi_readb(hdmi, HDMI_REVISION_ID) << 0);

	dw_hdmi_phy_power_off(hdmi);
	initialize_hdmi_mutes(hdmi);
}

static int dw_hdmi_read_edid(struct dw_hdmi *hdmi,
			     int block, unsigned char *buff)
{
	int i = 0, n = 0, index = 0, ret = -1, trytime = 2;
	int offset = (block % 2) * 0x80;
	int interrupt = 0;

	rockchip_dw_hdmi_i2cm_reset(hdmi);

	/* Set DDC I2C CLK which divided from DDC_CLK to 100KHz. */
	rockchip_dw_hdmi_i2cm_clk_init(hdmi);

	/* Enable I2C interrupt for reading edid */
	rockchip_dw_hdmi_i2cm_mask_int(hdmi, 0);

	hdmi_writeb(hdmi, DDC_I2C_EDID_ADDR, HDMI_I2CM_SLAVE);
	hdmi_writeb(hdmi, DDC_I2C_SEG_ADDR, HDMI_I2CM_SEGADDR);
	hdmi_writeb(hdmi, block / 2, HDMI_I2CM_SEGPTR);

	while (trytime--) {
		for (n = 0; n < HDMI_EDID_BLOCK_SIZE / 8; n++) {
			hdmi_writeb(hdmi, offset + 8 * n, HDMI_I2CM_ADDRESS);
			/*enable extend sequential read operation*/
			if (block == 0)
				hdmi_writeb(hdmi, HDMI_I2CM_OPERATION_READ8,
					    HDMI_I2CM_OPERATION);
			else
				hdmi_writeb(hdmi,
					    HDMI_I2CM_OPERATION_READ8_EXT,
					    HDMI_I2CM_OPERATION);

			i = 20;
			while (i--) {
				mdelay(1);
				interrupt = hdmi_readb(hdmi,
						       HDMI_IH_I2CM_STAT0);
				if (interrupt) {
					hdmi_writeb(hdmi,
						    interrupt,
						    HDMI_IH_I2CM_STAT0);
				}

				if (interrupt &
				    (m_SCDC_READREQ |
				     m_I2CM_DONE |
				     m_I2CM_ERROR))
					break;
				mdelay(4);
			}

			if (interrupt & m_I2CM_DONE) {
				for (index = 0; index < 8; index++)
					buff[8 * n + index] =
					hdmi_readb(hdmi, HDMI_I2CM_READ_BUFF0
						   + index);

				if (n == HDMI_EDID_BLOCK_SIZE / 8 - 1) {
					ret = 0;
					printf("[%s] edid read success\n",
					       __func__);
					goto exit;
				}
				continue;
			} else if ((interrupt & m_I2CM_ERROR) || (i == -1)) {
				printf("[%s] edid read error\n", __func__);
				rockchip_dw_hdmi_i2cm_reset(hdmi);
				break;
			}
		}

		printf("[%s] edid try times %d\n", __func__, trytime);
		mdelay(100);
	}

exit:
	/* Disable I2C interrupt */
	rockchip_dw_hdmi_i2cm_mask_int(hdmi, 1);
	return ret;
}

static int drm_do_get_edid(struct dw_hdmi *hdmi, u8 *edid)
{
	int i, j, block_num, ret;

	/* base block fetch */
	for (i = 0; i < 3; i++) {
		ret = dw_hdmi_read_edid(hdmi, 0, edid);
		if (!ret)
			break;
	}

	if (ret) {
		printf("get base block failed\n");
		goto err;
	}

	/* get the number of extensions */
	block_num = edid[0x7e];

	for (j = 1; j <= block_num; j++) {
		for (i = 0; i < 3; i++) {
			ret = dw_hdmi_read_edid(hdmi, j, &edid[0x80 * j]);
			if (!ret)
				break;
		}
	}

	if (ret) {
		printf("get extensions failed\n");
		goto err;
	}

	return 0;

err:
	memset(edid, 0, HDMI_EDID_BLOCK_SIZE);
	return -EFAULT;
}

void dw_hdmi_audio_enable(struct dw_hdmi *hdmi)
{
	hdmi->audio_enable = true;
	hdmi_set_cts_n(hdmi, hdmi->audio_cts, hdmi->audio_n);
}

void dw_hdmi_audio_disable(struct dw_hdmi *hdmi)
{
	hdmi->audio_enable = false;
	hdmi_set_cts_n(hdmi, hdmi->audio_cts, 0);
}

int rockchip_dw_hdmi_init(struct display_state *state)
{
	struct connector_state *conn_state = &state->conn_state;
	const struct rockchip_connector *connector = conn_state->connector;
	const struct dw_hdmi_plat_data *pdata = connector->data;
	struct crtc_state *crtc_state = &state->crtc_state;
	struct dw_hdmi *hdmi;
	struct drm_display_mode *mode_buf;
	int hdmi_node = conn_state->node;
	u32 val;

	hdmi = malloc(sizeof(struct dw_hdmi));
	if (!hdmi)
		return -ENOMEM;

	memset(hdmi, 0, sizeof(struct dw_hdmi));

	mode_buf = malloc(MODE_LEN * sizeof(struct drm_display_mode));
	if (!mode_buf)
		return -ENOMEM;
	memset(mode_buf, 0, MODE_LEN * sizeof(struct drm_display_mode));

	hdmi->regs = (void *)fdtdec_get_addr_size_auto_noparent(state->blob,
					hdmi_node, "reg", 0, NULL, false);
	hdmi->io_width = fdtdec_get_int(state->blob, hdmi_node,
					"reg-io-width", -1);
	hdmi->grf = syscon_get_first_range(ROCKCHIP_SYSCON_GRF);
	if (hdmi->grf <= 0) {
		printf("%s: Get syscon grf failed (ret=%p)\n",
		       __func__, hdmi->grf);
		return -ENXIO;
	}

	dw_hdmi_set_reg_wr(hdmi);

	if (crtc_state->crtc_id)
		val = ((1 << pdata->vop_sel_bit) |
		       (1 << (16 + pdata->vop_sel_bit)));
	else
		val = ((0 << pdata->vop_sel_bit) |
		       (1 << (16 + pdata->vop_sel_bit)));
	writel(val, hdmi->grf + pdata->grf_vop_sel_reg);

	conn_state->type = DRM_MODE_CONNECTOR_HDMIA;
	conn_state->output_mode = ROCKCHIP_OUT_MODE_AAAA;

	hdmi->dev_type = pdata->dev_type;
	hdmi->plat_data = pdata;
	hdmi->edid_data.mode_buf = mode_buf;
	hdmi->sample_rate = 48000;

	conn_state->private = hdmi;
	dw_hdmi_detect_phy(hdmi);
	dw_hdmi_dev_init(hdmi);

	return 0;
}

void rockchip_dw_hdmi_deinit(struct display_state *state)
{
	struct connector_state *conn_state = &state->conn_state;
	struct dw_hdmi *hdmi = conn_state->private;

	if (hdmi->edid_data.mode_buf)
		free(hdmi->edid_data.mode_buf);
	if (hdmi)
		free(hdmi);
}

int rockchip_dw_hdmi_prepare(struct display_state *state)
{
	return 0;
}

int rockchip_dw_hdmi_enable(struct display_state *state)
{
	struct connector_state *conn_state = &state->conn_state;
	struct drm_display_mode *mode = &conn_state->mode;
	struct dw_hdmi *hdmi = conn_state->private;

	if (!hdmi)
		return -EFAULT;

	dw_hdmi_setup(hdmi, mode);

	return 0;
}

int rockchip_dw_hdmi_disable(struct display_state *state)
{
	struct connector_state *conn_state = &state->conn_state;
	struct dw_hdmi *hdmi = conn_state->private;

	dw_hdmi_disable(hdmi);
	return 0;
}

int rockchip_dw_hdmi_get_timing(struct display_state *state)
{
	int ret;
	struct connector_state *conn_state = &state->conn_state;
	struct drm_display_mode *mode = &conn_state->mode;
	struct dw_hdmi *hdmi = conn_state->private;

	if (!hdmi)
		return -EFAULT;
	ret = drm_do_get_edid(hdmi, conn_state->edid);
	if (!ret) {
		ret = drm_add_edid_modes(&hdmi->edid_data, conn_state->edid);

		if (ret > 0) {
			hdmi->sink_is_hdmi =
				drm_detect_hdmi_monitor(conn_state->edid);
			hdmi->sink_has_audio = false;
			*mode = *hdmi->edid_data.preferred_mode;
			hdmi->vic = drm_match_cea_mode(mode);

			return 0;
		}
	}

	/* if can't get edid timing, use default resolution. */
	printf("can't get edid timing\n");
	hdmi->vic = HDMI_VIDEO_DEFAULT_MODE;
	hdmi->sink_is_hdmi = true;
	hdmi->sink_has_audio = false;
	mode->hdisplay = 1280;
	mode->hsync_start = 1390;
	mode->hsync_end = 1430;
	mode->htotal = 1650;
	mode->vdisplay = 720;
	mode->vsync_start = 725;
	mode->vsync_end = 730;
	mode->vtotal = 750;
	mode->clock = 74250;
	mode->flags = DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC;

	return 0;
}

int rockchip_dw_hdmi_detect(struct display_state *state)
{
	int ret;
	struct connector_state *conn_state = &state->conn_state;
	struct dw_hdmi *hdmi = conn_state->private;

	if (!hdmi)
		return -EFAULT;

	ret = dw_hdmi_detect_hotplug(hdmi);

	return ret;
}

int rockchip_dw_hdmi_get_edid(struct display_state *state)
{
	int ret;
	struct connector_state *conn_state = &state->conn_state;
	struct dw_hdmi *hdmi = conn_state->private;

	ret = drm_do_get_edid(hdmi, conn_state->edid);

	return ret;
}

