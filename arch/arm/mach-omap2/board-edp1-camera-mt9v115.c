/*
 * linux/arch/arm/mach-omap2/board-edp1-camera.c
 *
 * Copyright (C) 2007 Texas Instruments
 *
 * Modified from mach-omap2/board-generic.c
 *
 * Initial code: Syed Mohammed Khasim
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/clk.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/mm.h>
#include <linux/regulator/consumer.h>

#if defined(CONFIG_TWL4030_CORE) && defined(CONFIG_VIDEO_OMAP3)

#include <linux/i2c/twl.h>

#include <asm/io.h>

#include <mach/gpio.h>
#include <plat/omap-pm.h>

#include <media/v4l2-int-device.h>
#include <../drivers/media/video/omap34xxcam.h>
#include <../drivers/media/video/isp/ispreg.h>
#define DEBUG_BASE		0x08000000

/*	GPIO_111 */
#define CAMZOOM2_USE_XCLKB  	1

#define ISP_MT9V115_MCLK		72000000

/* Sensor specific GPIO signals */
#if defined(CONFIG_CL1_DVT_BOARD)
#define MT9V115_PWDN_GPIO  		96
#else
#define MT9V115_PWDN_GPIO  		161
#endif
//#define MT9V115_RESET_GPIO  	45
#define MT9V115_SELECT_GPIO  	47
//#define MT9V115_OE_GPIO  		44
#define SELECT_MT9V115			0

#if defined(CONFIG_VIDEO_MT9V115) || defined(CONFIG_VIDEO_MT9V115_MODULE)
#include <media/mt9v115.h>
#include <../drivers/media/video/isp/ispcsi2.h>
#define MT9V115_CSI2_CLOCK_POLARITY	0	/* +/- pin order */
#define MT9V115_CSI2_DATA0_POLARITY	0	/* +/- pin order */
#define MT9V115_CSI2_DATA1_POLARITY	0	/* +/- pin order */
#define MT9V115_CSI2_CLOCK_LANE		1	 /* Clock lane position: 1 */
#define MT9V115_CSI2_DATA0_LANE		2	 /* Data0 lane position: 2 */
#define MT9V115_CSI2_DATA1_LANE		3	 /* Data1 lane position: 3 */
#define MT9V115_CSI2_PHY_THS_TERM	2
#define MT9V115_CSI2_PHY_THS_SETTLE	23
#define MT9V115_CSI2_PHY_TCLK_TERM	0
#define MT9V115_CSI2_PHY_TCLK_MISS	1
#define MT9V115_CSI2_PHY_TCLK_SETTLE	14
#define MT9V115_BIGGEST_FRAME_BYTE_SIZE	PAGE_ALIGN(ALIGN(2048, 0x20) * 1536 * 2)
#endif

/* <-- LH_SWRD_CL1_Henry@2011.12.1 check front camera power state  */	
bool global_frontcamera_power=false;

#if defined(CONFIG_VIDEO_MT9V115) || defined(CONFIG_VIDEO_MT9V115_MODULE)

extern int evt_cam_regulator(struct device *dev, int enable);

static void mt9v115_msleep(unsigned long ms)
{
	unsigned long us = ms * 1000;

	usleep_range(us, us + 1000);
}

static struct omap34xxcam_sensor_config mt9v115_hwc = {
	.sensor_isp  = 0,
	.capture_mem = MT9V115_BIGGEST_FRAME_BYTE_SIZE * 4,
	.ival_default	= { 1, 10 },
	.isp_if = ISP_CSIA,
};

static int mt9v115_sensor_set_prv_data(struct v4l2_int_device *s, void *priv)
{
	struct omap34xxcam_hw_config *hwc = priv;

	hwc->u.sensor		= mt9v115_hwc;
	hwc->dev_index		= 0;
	hwc->dev_minor		= 0;
	hwc->dev_type		= OMAP34XXCAM_SLAVE_SENSOR;

	return 0;
}

static struct isp_interface_config mt9v115_if_config = {
	.ccdc_par_ser 		= ISP_CSIA,
	.dataline_shift 	= 0x0,
	.hsvs_syncdetect 	= ISPCTRL_SYNC_DETECT_VSRISE,
	.strobe 		= 0x0,
	.prestrobe 		= 0x0,
	.shutter 		= 0x0,
	.wenlog 		= ISPCCDC_CFG_WENLOG_AND,
	.wait_hs_vs		= 0,
	.cam_mclk		= ISP_MT9V115_MCLK,
	.raw_fmt_in		= ISPCCDC_INPUT_FMT_RG_GB, //not used
	.u.csi.crc 		= 0x0,
	.u.csi.mode 		= 0x0,
	.u.csi.edge 		= 0x0,
	.u.csi.signalling 	= 0x0,
	.u.csi.strobe_clock_inv = 0x0,
	.u.csi.vs_edge 		= 0x0,
	.u.csi.channel 		= 0x0,
	.u.csi.vpclk 		= 0x2,
	.u.csi.data_start 	= 0x0,
	.u.csi.data_size 	= 0x0,
	.u.csi.format 		= V4L2_PIX_FMT_UYVY,//V4L2_PIX_FMT_SGRBG10,	
};


static int mt9v115_sensor_power_set(struct v4l2_int_device *s, enum v4l2_power power)
{
	struct omap34xxcam_videodev *vdev = s->u.slave->master->priv;
	struct isp_device *isp = dev_get_drvdata(vdev->cam->isp);
	struct isp_csi2_lanes_cfg lanecfg;
	struct isp_csi2_phy_cfg phyconfig;
	static enum v4l2_power previous_power = V4L2_POWER_OFF;
	static struct pm_qos_request_list *qos_request;
	int err = 0;

	printk(" [%s] \n", __func__);
	
	switch (power) {
	case V4L2_POWER_ON:
/* <-- LH_SWRD_CL1_Henry@2011.12.1 set  back camera power state  */	
		global_frontcamera_power=true;
		/* Power Up Sequence */
		printk(KERN_ERR "mt9v115_sensor_power_set(ON)\n");

		/*
		 * Through-put requirement:
		 * Set max OCP freq for 3630 is 200 MHz through-put
		 * is in KByte/s so 200000 KHz * 4 = 800000 KByte/s
		 */
		omap_pm_set_min_bus_tput(vdev->cam->isp,
					 OCP_INITIATOR_AGENT, 800000);

		/* Hold a constraint to keep MPU in C1 */
		//omap_pm_set_max_mpu_wakeup_lat(&qos_request, 12);

		isp_csi2_reset(&isp->isp_csi2);
		/* Request and configure gpio pins */
		/*if (gpio_request(MT9V115_SELECT_GPIO, "mt9v115_select") != 0)
		{
			printk(KERN_ERR "request MT9V115_SELECT_GPIO error\n");
			return -EIO;
		}*/

		/*if (gpio_request(MT9V115_RESET_GPIO, "mt9v115_rst") != 0)
		{
			return -EIO;
		}*/

		/*if (gpio_request(MT9V115_PWDN_GPIO, "mt9v115_ps") != 0)
		{
			printk(KERN_ERR "request MT9V115_PWDN_GPIO error\n");
			return -EIO;
		}*/
		/* Request and configure gpio pins */
		/*if (gpio_request(MT9V115_SELECT_GPIO, "mt9v115_select") != 0)
		{
			printk(KERN_ERR "request MT9V115_SELECT_GPIO error\n");
			return -EIO;
		}*/

		/*if (gpio_request(MT9V115_RESET_GPIO, "mt9v115_rst") != 0)
		{
			return -EIO;
		}*/

		/*if (gpio_request(MT9V115_PWDN_GPIO, "mt9v115_ps") != 0)
		{
			printk(KERN_ERR "request MT9V115_PWDN_GPIO error\n");
			return -EIO;
		}*/

		//mdelay(100);
		mt9v115_msleep(100);
		lanecfg.clk.pol = MT9V115_CSI2_CLOCK_POLARITY;
		lanecfg.clk.pos = MT9V115_CSI2_CLOCK_LANE;
		lanecfg.data[0].pol = MT9V115_CSI2_DATA0_POLARITY;
		lanecfg.data[0].pos = MT9V115_CSI2_DATA0_LANE;
		lanecfg.data[1].pol = 0;
		lanecfg.data[1].pos = 0;
		lanecfg.data[2].pol = 0;
		lanecfg.data[2].pos = 0;
		lanecfg.data[3].pol = 0;
		lanecfg.data[3].pos = 0;
		isp_csi2_complexio_lanes_config(&isp->isp_csi2, &lanecfg);
		isp_csi2_complexio_lanes_update(&isp->isp_csi2, true);

		isp_csi2_ctrl_config_ecc_enable(&isp->isp_csi2, true);

		phyconfig.ths_term = MT9V115_CSI2_PHY_THS_TERM;
		phyconfig.ths_settle = MT9V115_CSI2_PHY_THS_SETTLE;
		phyconfig.tclk_term = MT9V115_CSI2_PHY_TCLK_TERM;
		phyconfig.tclk_miss = MT9V115_CSI2_PHY_TCLK_MISS;
		phyconfig.tclk_settle = MT9V115_CSI2_PHY_TCLK_SETTLE;
		isp_csi2_phy_config(&isp->isp_csi2, &phyconfig);
		isp_csi2_phy_update(&isp->isp_csi2, true);

		isp_configure_interface(vdev->cam->isp, &mt9v115_if_config);
		//mdelay(1);
		mt9v115_msleep(1);
		break;
		
	case V4L2_POWER_STANDBY:
		/* Power Up Sequence */
		printk(KERN_DEBUG "mt9v115_sensor_power_set(STANDBY)\n");

		if (previous_power == V4L2_POWER_STANDBY)
		{
			/* We have power on camera, nothing to do. */
			break;
		}
		
		if (previous_power == V4L2_POWER_ON)
		{
			isp_csi2_complexio_power_autoswitch(&isp->isp_csi2, false);
			isp_csi2_complexio_power(&isp->isp_csi2, ISP_CSI2_POWER_OFF);
			omap_pm_set_min_bus_tput(vdev->cam->isp, OCP_INITIATOR_AGENT, 0);
			//omap_pm_set_max_mpu_wakeup_lat(&qos_request, -1);
			isp_disable_mclk(isp);
			
			break;
		}
		
		//evt_cam_regulator(isp->dev, 1);

		/* Request and configure gpio pins */
		/*if (gpio_request(MT9V115_SELECT_GPIO, "mt9v115_select") != 0)
		{
			printk(KERN_ERR "request MT9V115_SELECT_GPIO error\n");
			return -EIO;
		}*/

		/*if (gpio_request(MT9V115_RESET_GPIO, "mt9v115_rst") != 0)
		{
			return -EIO;
		}*/

		/*if (gpio_request(MT9V115_PWDN_GPIO, "mt9v115_ps") != 0)
		{
			printk(KERN_ERR "request MT9V115_PWDN_GPIO error\n");
			return -EIO;
		}*/

		/* Select mt9v115. */
		gpio_set_value(MT9V115_SELECT_GPIO, 0);

		/* set to output mode */
		gpio_direction_output(MT9V115_SELECT_GPIO, true);
		
		gpio_set_value(MT9V115_SELECT_GPIO, 0);

		//udelay(100);
		usleep_range(100, 150);
		
		/* Enable camera bus connect. */
		//gpio_set_value(MT9V115_OE_GPIO, 0);

		/* set to output mode */
		//gpio_direction_output(MT9V115_OE_GPIO, true);

		//udelay(100);

		/* nRESET is active LOW. set HIGH to release reset */
		//gpio_set_value(MT9V115_RESET_GPIO, 1);

		/* set to output mode */
		//gpio_direction_output(MT9V115_RESET_GPIO, true);

		//udelay(100);

		/* PWDN is active High. set LOW to enable power of sensor */
		gpio_set_value(MT9V115_PWDN_GPIO, 0);

		/* set to output mode */
		gpio_direction_output(MT9V115_PWDN_GPIO, true);
		
		gpio_set_value(MT9V115_PWDN_GPIO, 0);

		//mdelay(10);
		mt9v115_msleep(10);
		

		/* have to put sensor to reset to guarantee detection */
		//gpio_set_value(MT9V115_RESET_GPIO, 0);
		//udelay(1500);

		/* nRESET is active LOW. set HIGH to release reset */
		//gpio_set_value(MT9V115_RESET_GPIO, 1);
		//udelay(300);
		
		break;
	case V4L2_POWER_OFF:
/* <-- LH_SWRD_CL1_Henry@2011.12.1 set  back camera power state  */	
		global_frontcamera_power=false;
		printk(KERN_ERR "mt9v115_sensor_power_set(%s)\n",
			(power == V4L2_POWER_OFF) ? "OFF" : "STANDBY");
		/* Power Down Sequence */
		if (previous_power == V4L2_POWER_ON)
		{
			isp_csi2_complexio_power(&isp->isp_csi2, ISP_CSI2_POWER_OFF);
		}

		//gpio_free(MT9V115_RESET_GPIO);
		
		//udelay(100);

		/* PWDN is active High. set HIGH to disable power of sensor */
		gpio_set_value(MT9V115_PWDN_GPIO, 1);

		/* set to output mode */
		gpio_direction_output(MT9V115_PWDN_GPIO, true);
		
		gpio_set_value(MT9V115_PWDN_GPIO, 1);
		//mdelay(10);
		mt9v115_msleep(10);

		/* Disconnect camera bus. */
		//gpio_set_value(MT9V115_OE_GPIO, 1);
		//udelay(100);
		
		//gpio_free(MT9V115_PWDN_GPIO);
		//gpio_free(MT9V115_SELECT_GPIO);

		/* Remove pm constraints */
		if (previous_power == V4L2_POWER_ON)
		{
			omap_pm_set_min_bus_tput(vdev->cam->isp, OCP_INITIATOR_AGENT, 0);
			//omap_pm_set_max_mpu_wakeup_lat(&qos_request, -1);
		}

		/* Make sure not to disable the MCLK twice in a row */
		if (previous_power == V4L2_POWER_ON)
		{
			isp_disable_mclk(isp);
		}	

		//evt_cam_regulator(isp->dev, 0);

		break;
	}

	/* Save powerstate to know what was before calling POWER_ON. */
	previous_power = power;
	return err;
}

static u32 mt9v115_sensor_set_xclk(struct v4l2_int_device *s, u32 xclkfreq)
{
	struct omap34xxcam_videodev *vdev = s->u.slave->master->priv;
	printk(" [%s] \n", __func__);
	return isp_set_xclk(vdev->cam->isp, xclkfreq, CAMZOOM2_USE_XCLKB);
}

static int mt9v115_csi2_lane_count(struct v4l2_int_device *s, int count)
{
	struct omap34xxcam_videodev *vdev = s->u.slave->master->priv;
	struct isp_device *isp = dev_get_drvdata(vdev->cam->isp);
	printk(" [%s] \n", __func__);
	return isp_csi2_complexio_lanes_count(&isp->isp_csi2, count);
}

static int mt9v115_csi2_cfg_vp_out_ctrl(struct v4l2_int_device *s,
				       u8 vp_out_ctrl)
{
	struct omap34xxcam_videodev *vdev = s->u.slave->master->priv;
	struct isp_device *isp = dev_get_drvdata(vdev->cam->isp);
	printk(" [%s] \n", __func__);
	return isp_csi2_ctrl_config_vp_out_ctrl(&isp->isp_csi2, vp_out_ctrl);
}

static int mt9v115_csi2_ctrl_update(struct v4l2_int_device *s, bool force_update)
{
	struct omap34xxcam_videodev *vdev = s->u.slave->master->priv;
	struct isp_device *isp = dev_get_drvdata(vdev->cam->isp);
	printk(" [%s] \n", __func__);
	return isp_csi2_ctrl_update(&isp->isp_csi2, force_update);
}

static int mt9v115_csi2_cfg_virtual_id(struct v4l2_int_device *s, u8 ctx, u8 id)
{
	struct omap34xxcam_videodev *vdev = s->u.slave->master->priv;
	struct isp_device *isp = dev_get_drvdata(vdev->cam->isp);
	printk(" [%s] \n", __func__);
	return isp_csi2_ctx_config_virtual_id(&isp->isp_csi2, ctx, id);
}

static int mt9v115_csi2_ctx_update(struct v4l2_int_device *s, u8 ctx,
				  bool force_update)
{
	struct omap34xxcam_videodev *vdev = s->u.slave->master->priv;
	struct isp_device *isp = dev_get_drvdata(vdev->cam->isp);
	printk(" [%s] \n", __func__);
	return isp_csi2_ctx_update(&isp->isp_csi2, ctx, force_update);
}

static int mt9v115_csi2_calc_phy_cfg0(struct v4l2_int_device *s,
				     u32 mipiclk, u32 lbound_hs_settle,
				     u32 ubound_hs_settle)
{
	struct omap34xxcam_videodev *vdev = s->u.slave->master->priv;
	struct isp_device *isp = dev_get_drvdata(vdev->cam->isp);
	printk(" [%s] \n", __func__);
	return isp_csi2_calc_phy_cfg0(&isp->isp_csi2, mipiclk,
				      lbound_hs_settle, ubound_hs_settle);
}

struct mt9v115_platform_data zoom2_mt9v115_platform_data = {
	.power_set            = mt9v115_sensor_power_set,			//boot 1
	.priv_data_set        = mt9v115_sensor_set_prv_data,
	.set_xclk             = mt9v115_sensor_set_xclk,			//boot 1
	.csi2_lane_count      = mt9v115_csi2_lane_count,			//1
	.csi2_cfg_vp_out_ctrl = mt9v115_csi2_cfg_vp_out_ctrl,			//1
	.csi2_ctrl_update     = mt9v115_csi2_ctrl_update,			//1
	.csi2_cfg_virtual_id  = mt9v115_csi2_cfg_virtual_id,			//1
	.csi2_ctx_update      = mt9v115_csi2_ctx_update,			//1
	.csi2_calc_phy_cfg0   = mt9v115_csi2_calc_phy_cfg0,			//1
};
#endif

void __init zoom2_cam_mt9v115_init(void)
{
	printk(KERN_ERR "zoom2_cam_mt9v115_init\n");
	printk(" [%s] \n", __func__);
	if (gpio_request(MT9V115_SELECT_GPIO, "mt9v115_select") != 0)
	{
		printk(KERN_ERR "request MT9V115_SELECT_GPIO error\n");
	}

	if (gpio_request(MT9V115_PWDN_GPIO, "mt9v115_ps") != 0)
	{
		printk(KERN_ERR "request MT9V115_PWDN_GPIO error\n");
	}
}

#endif
