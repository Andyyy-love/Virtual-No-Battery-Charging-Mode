  // SPDX-License-Identifier: GPL-2.0

/*
 * Copyright (c) 2021 MediaTek Inc.
 */

#include <linux/types.h>
#include <linux/init.h>		/* For init/exit macros */
#include <linux/module.h>	/* For MODULE_ marcros  */
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#ifdef CONFIG_OF
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#endif
//#include <mt-plat/mtk_boot.h>
//#include <mt-plat/upmu_common.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/power_supply.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/machine.h>
#include "sgm415xx.h"
#include "charger_class.h"
#include "mtk_charger.h"
#include <linux/regmap.h>
#include "../../misc/mediatek/usb/usb20/mtk_musb.h"
#include <linux/iio/consumer.h>
#include <linux/sched/clock.h>
/**********************************************************
 *
 *   [I2C Slave Setting]
 *
 *********************************************************/

#define SGM4154x_REG_NUM    (0xF)

char Exists_IC_Name[20];

extern int get_entry_ship_mode(void);

/* SGM4154x REG06 BOOST_LIM[5:4], uV */
static const unsigned int BOOST_VOLT_LIMIT[] = {
	4850000, 5000000, 5150000, 5300000		
};
 /* SGM4154x REG02 BOOST_LIM[7:7], uA */
#if (defined(__SGM41542_CHIP_ID__) || defined(__SGM41541_CHIP_ID__)|| defined(__SGM41543_CHIP_ID__)|| defined(__SGM41543D_CHIP_ID__))
static const unsigned int BOOST_CURRENT_LIMIT[] = {
	1200000, 2000000
};
#else
static const unsigned int BOOST_CURRENT_LIMIT[] = {
	500000, 1200000
};
#endif

extern void Charger_Detect_Init(void);
extern void Charger_Detect_Release(void);

#if (defined(__SGM41513_CHIP_ID__) || defined(__SGM41513A_CHIP_ID__) || defined(__SGM41513D_CHIP_ID__))

static const unsigned int IPRECHG_CURRENT_STABLE[] = {
	5000, 10000, 15000, 20000, 30000, 40000, 50000, 60000,
	80000, 100000, 120000, 140000, 160000, 180000, 200000, 240000
};

static const unsigned int ITERM_CURRENT_STABLE[] = {
	5000, 10000, 15000, 20000, 30000, 40000, 50000, 60000,
	80000, 100000, 120000, 140000, 160000, 180000, 200000, 240000
};
#endif

#define CPU_CLOCK_TIME_MS 1000000
#define HVDCP_DETECT_TO_DETACH_TIME 800

static enum power_supply_usb_type sgm4154x_usb_type[] = {
        POWER_SUPPLY_USB_TYPE_UNKNOWN,
        POWER_SUPPLY_USB_TYPE_SDP,              /* Standard Downstream Port */
        POWER_SUPPLY_USB_TYPE_DCP,              /* Dedicated Charging Port */
        POWER_SUPPLY_USB_TYPE_CDP,              /* Charging Downstream Port */
        POWER_SUPPLY_USB_TYPE_ACA,              /* Accessory Charger Adapters */
        POWER_SUPPLY_USB_TYPE_C,                /* Type C Port */
        POWER_SUPPLY_USB_TYPE_PD,               /* Power Delivery Port */
        POWER_SUPPLY_USB_TYPE_PD_DRP,           /* PD Dual Role Port */
        POWER_SUPPLY_USB_TYPE_PD_PPS,           /* PD Programmable Power Supply */
        POWER_SUPPLY_USB_TYPE_APPLE_BRICK_ID,	
};

static const struct charger_properties sgm4154x_chg_props = {
	.alias_name = SGM4154x_NAME,
};


enum {
	SGM_DP_DM_VOL_HIZ,
	SGM_DP_DM_VOL_0P0,
	SGM_DP_DM_VOL_0P6,
	SGM_DP_DM_VOL_3P3,
};

enum SGM4154x_QC_VOLT {
	QC_20_5000mV,
	QC_20_9000mV,
	QC_20_12000mV,
};

/*Barley code for OBARLEY-6460 by liyw37 at 20230904 start*/
enum vindpm_track {
    SGM4154x_TRACK_DIS,
    SGM4154x_TRACK_200,
    SGM4154x_TRACK_250,
    SGM4154x_TRACK_300,
};
/*Barley code for OBARLEY-6460 by liyw37 at 20230904 end*/


/**********************************************************
 *
 *   [Global Variable]
 *
 *********************************************************/
static struct power_supply_desc sgm4154x_power_supply_desc;
static struct charger_device *s_chg_dev_otg;

/**********************************************************
 *
 *   [I2C Function For Read/Write sgm4154x]
 *
 *********************************************************/
static int __sgm4154x_read_byte(struct sgm4154x_device *sgm, u8 reg, u8 *data)
{
    s32 ret;

    ret = i2c_smbus_read_byte_data(sgm->client, reg);
    if (ret < 0) {
        pr_err("i2c read fail: can't read from reg 0x%02X\n", reg);
        return ret;
    }

    *data = (u8) ret;

    return 0;
}

static int __sgm4154x_write_byte(struct sgm4154x_device *sgm, int reg, u8 val)
{
    s32 ret;

    ret = i2c_smbus_write_byte_data(sgm->client, reg, val);
    if (ret < 0) {
        pr_err("i2c write fail: can't write 0x%02X to reg 0x%02X: %d\n",
               val, reg, ret);
        return ret;
    }
    return 0;
}

static int sgm4154x_read_reg(struct sgm4154x_device *sgm, u8 reg, u8 *data)
{
	int ret;

	mutex_lock(&sgm->i2c_rw_lock);
	ret = __sgm4154x_read_byte(sgm, reg, data);
	mutex_unlock(&sgm->i2c_rw_lock);

	return ret;
}
#if 0
static int sgm4154x_write_reg(struct sgm4154x_device *sgm, u8 reg, u8 val)
{
	int ret;

	mutex_lock(&sgm->i2c_rw_lock);
	ret = __sgm4154x_write_byte(sgm, reg, val);
	mutex_unlock(&sgm->i2c_rw_lock);

	if (ret)
		pr_err("Failed: reg=%02X, ret=%d\n", reg, ret);

	return ret;
}
#endif
static int sgm4154x_update_bits(struct sgm4154x_device *sgm, u8 reg,
					u8 mask, u8 val)
{
	int ret;
	u8 tmp;

	mutex_lock(&sgm->i2c_rw_lock);
	ret = __sgm4154x_read_byte(sgm, reg, &tmp);
	if (ret) {
		pr_err("Failed: reg=%02X, ret=%d\n", reg, ret);
		goto out;
	}

	tmp &= ~mask;
	tmp |= val & mask;
	
	if(reg == SGM4154x_CHRG_CTRL_0) {
		// tmp &= ~SGM4154x_HIZ_EN;
	}

	ret = __sgm4154x_write_byte(sgm, reg, tmp);
	if (ret)
		pr_err("Failed: reg=%02X, ret=%d\n", reg, ret);

out:
	mutex_unlock(&sgm->i2c_rw_lock);
	return ret;
}

/**********************************************************
 *
 *   [Internal Function]
 *
 *********************************************************/

 static int sgm4154x_set_watchdog_timer(struct sgm4154x_device *sgm, int time)
{
	int ret;
	u8 reg_val;

	if (time == 0)
		reg_val = SGM4154x_WDT_TIMER_DISABLE;
	else if (time == 40)
		reg_val = SGM4154x_WDT_TIMER_40S;
	else if (time == 80)
		reg_val = SGM4154x_WDT_TIMER_80S;
	else
		reg_val = SGM4154x_WDT_TIMER_160S;	

	ret = sgm4154x_update_bits(sgm, SGM4154x_CHRG_CTRL_5,
				SGM4154x_WDT_TIMER_MASK, reg_val);

	return ret;
}

 #if 0
 static int sgm4154x_get_term_curr(struct sgm4154x_device *sgm)
{
	int ret;
	u8 reg_val;
	int curr;
	int offset = SGM4154x_TERMCHRG_I_MIN_uA;

	ret = sgm4154x_read_reg(sgm, SGM4154x_CHRG_CTRL_3, &reg_val);
	if (ret)
		return ret;

	reg_val &= SGM4154x_TERMCHRG_CUR_MASK;
	curr = reg_val * SGM4154x_TERMCHRG_CURRENT_STEP_uA + offset;
	return curr;
}

static int sgm4154x_get_prechrg_curr(struct sgm4154x_device *sgm)
{
	int ret;
	u8 reg_val;
	int curr;
	int offset = SGM4154x_PRECHRG_I_MIN_uA;

	ret = sgm4154x_read_reg(sgm, SGM4154x_CHRG_CTRL_3, &reg_val);
	if (ret)
		return ret;

	reg_val = (reg_val&SGM4154x_PRECHRG_CUR_MASK)>>4;
	curr = reg_val * SGM4154x_PRECHRG_CURRENT_STEP_uA + offset;
	return curr;
}

#endif

static int sgm4154x_set_term_curr(struct charger_device *chg_dev, u32 uA)
{
	u8 reg_val;
	struct sgm4154x_device *sgm = charger_get_data(chg_dev);
#if (defined(__SGM41513_CHIP_ID__) || defined(__SGM41513A_CHIP_ID__) || defined(__SGM41513D_CHIP_ID__))	
	
	for(reg_val = 1; reg_val < 16 && uA >= ITERM_CURRENT_STABLE[reg_val]; reg_val++)
		;
	reg_val--;
#else
	if (uA < SGM4154x_TERMCHRG_I_MIN_uA)
		uA = SGM4154x_TERMCHRG_I_MIN_uA;
	else if (uA > SGM4154x_TERMCHRG_I_MAX_uA)
		uA = SGM4154x_TERMCHRG_I_MAX_uA;
	
	reg_val = (uA - SGM4154x_TERMCHRG_I_MIN_uA) / SGM4154x_TERMCHRG_CURRENT_STEP_uA;
#endif

	return sgm4154x_update_bits(sgm, SGM4154x_CHRG_CTRL_3,
				  SGM4154x_TERMCHRG_CUR_MASK, reg_val);
}

static int sgm4154x_set_vreg_ft(struct charger_device *chg_dev, bool temp)
{
	u8 reg_val;
	u8 vreg_val;
	struct sgm4154x_device *sgm = charger_get_data(chg_dev);

	if(temp) {
		reg_val = SGM4154x_VREG_FT_EN_SHIFT;
		vreg_val = SGM4154x_VREG_LOWTMP_CV << SGM4154x_VREG_CV_OFFSET;
		sgm4154x_update_bits(sgm, SGM4154x_CHRG_CTRL_4,
				  SGM4154x_VREG_V_MASK, vreg_val);
	} else {
		reg_val = SGM4154x_VREG_FT_SHIFT;
	}
	reg_val = reg_val << SGM4154x_VREG_FT_OFFSET;

	return sgm4154x_update_bits(sgm, SGM4154x_CHRG_CTRL_f,
				  SGM4154x_VREG_FT_MASK, reg_val);
}

static int sgm4154x_set_prechrg_curr(struct sgm4154x_device *sgm, int uA)
{
	u8 reg_val;
	
#if (defined(__SGM41513_CHIP_ID__) || defined(__SGM41513A_CHIP_ID__) || defined(__SGM41513D_CHIP_ID__))
	for(reg_val = 1; reg_val < 16 && uA >= IPRECHG_CURRENT_STABLE[reg_val]; reg_val++)
		;
	reg_val--;
#else
	if (uA < SGM4154x_PRECHRG_I_MIN_uA)
		uA = SGM4154x_PRECHRG_I_MIN_uA;
	else if (uA > SGM4154x_PRECHRG_I_MAX_uA)
		uA = SGM4154x_PRECHRG_I_MAX_uA;

	reg_val = (uA - SGM4154x_PRECHRG_I_MIN_uA) / SGM4154x_PRECHRG_CURRENT_STEP_uA;
#endif
	reg_val = reg_val << 4;
	return sgm4154x_update_bits(sgm, SGM4154x_CHRG_CTRL_3,
				  SGM4154x_PRECHRG_CUR_MASK, reg_val);
}

static int sgm4154x_get_ichg_curr(struct charger_device *chg_dev, u32 *uA)
{
	int ret;
	u8 ichg;
    u32 curr;
	
	struct sgm4154x_device *sgm = charger_get_data(chg_dev);
	
	ret = sgm4154x_read_reg(sgm, SGM4154x_CHRG_CTRL_2, &ichg);
	if (ret)
		return ret;	

	ichg &= SGM4154x_ICHRG_I_MASK;
#if (defined(__SGM41513_CHIP_ID__) || defined(__SGM41513A_CHIP_ID__) || defined(__SGM41513D_CHIP_ID__))	
	if (ichg <= 0x8)
		curr = ichg * 5000;
	else if (ichg <= 0xF)
		curr = 40000 + (ichg - 0x8) * 10000;
	else if (ichg <= 0x17)
		curr = 110000 + (ichg - 0xF) * 20000;
	else if (ichg <= 0x20)
		curr = 270000 + (ichg - 0x17) * 30000;
	else if (ichg <= 0x30)
		curr = 540000 + (ichg - 0x20) * 60000;
	else if (ichg <= 0x3C)
		curr = 1500000 + (ichg - 0x30) * 120000;
	else
		curr = 3000000;
#else
	curr = ichg * SGM4154x_ICHRG_I_STEP_uA;
#endif	
	*uA = curr;
	return 0;
}

static int sgm4154x_set_ichrg_curr(struct charger_device *chg_dev, unsigned int uA)
{
	int ret;
	u8 reg_val;
	struct sgm4154x_device *sgm = charger_get_data(chg_dev);
	
	if (uA < SGM4154x_ICHRG_I_MIN_uA)
		uA = SGM4154x_ICHRG_I_MIN_uA;
	else if ( uA > sgm->init_data.max_ichg)
		uA = sgm->init_data.max_ichg;

	pr_err("sgm4154x_set_ichrg_curr enter uA = %d\n", uA);

#if (defined(__SGM41513_CHIP_ID__) || defined(__SGM41513A_CHIP_ID__) || defined(__SGM41513D_CHIP_ID__))
	if (uA <= 40000)
		reg_val = uA / 5000;	
	else if (uA <= 110000)
		reg_val = 0x08 + (uA -40000) / 10000;	
	else if (uA <= 270000)
		reg_val = 0x0F + (uA -110000) / 20000;	
	else if (uA <= 540000)
		reg_val = 0x17 + (uA -270000) / 30000;	
	else if (uA <= 1500000)
		reg_val = 0x20 + (uA -540000) / 60000;	
	else if (uA <= 2940000)
		reg_val = 0x30 + (uA -1500000) / 120000;
	else 
		reg_val = 0x3d;
	
	pr_err("sgm4154x_set_ichrg_curr first reg_val = %02x\n", reg_val);
#else

	reg_val = uA / SGM4154x_ICHRG_I_STEP_uA;

	pr_err("sgm4154x_set_ichrg_curr second reg_val = %02x\n", reg_val);

	
#endif
	pr_err("sgm4154x_set_ichrg_curr third reg_val = %02x\n", reg_val);

	ret = sgm4154x_update_bits(sgm, SGM4154x_CHRG_CTRL_2,
				  SGM4154x_ICHRG_I_MASK, reg_val);
	
	return ret;
}

static int sgm4154x_set_chrg_volt(struct charger_device *chg_dev, u32 chrg_volt)
{
	int ret;
	u8 reg_val;
	struct sgm4154x_device *sgm = charger_get_data(chg_dev);
	
	if (chrg_volt < SGM4154x_VREG_V_MIN_uV)
		chrg_volt = SGM4154x_VREG_V_MIN_uV;
	else if (chrg_volt > sgm->init_data.max_vreg)
		chrg_volt = sgm->init_data.max_vreg;
	
	
	reg_val = (chrg_volt-SGM4154x_VREG_V_MIN_uV) / SGM4154x_VREG_V_STEP_uV;
	reg_val = reg_val<<3;
	ret = sgm4154x_update_bits(sgm, SGM4154x_CHRG_CTRL_4,
				  SGM4154x_VREG_V_MASK, reg_val);

	return ret;
}

static int sgm4154x_get_chrg_volt(struct charger_device *chg_dev,unsigned int *volt)
{
	int ret;
	u8 vreg_val;
	struct sgm4154x_device *sgm = charger_get_data(chg_dev);
	
	ret = sgm4154x_read_reg(sgm, SGM4154x_CHRG_CTRL_4, &vreg_val);
	if (ret)
		return ret;	

	vreg_val = (vreg_val & SGM4154x_VREG_V_MASK)>>3;

	if (15 == vreg_val)
		*volt = 4352000; //default
	else if (vreg_val < 25)	
		*volt = vreg_val*SGM4154x_VREG_V_STEP_uV + SGM4154x_VREG_V_MIN_uV;	

	return 0;
}

#if (0)
static int sgm4154x_get_vindpm_offset_os(struct sgm4154x_device *sgm)
{
	int ret;
	u8 reg_val;

	ret = sgm4154x_read_reg(sgm, SGM4154x_CHRG_CTRL_f, &reg_val);
	if (ret)
		return ret;	

	reg_val = reg_val & SGM4154x_VINDPM_OS_MASK;	

	return reg_val;
}
#endif

static int sgm4154x_set_vindpm_offset_os(struct sgm4154x_device *sgm,u8 offset_os)
{
	int ret;	
	
	ret = sgm4154x_update_bits(sgm, SGM4154x_CHRG_CTRL_f,
				  SGM4154x_VINDPM_OS_MASK, offset_os);
	
	if (ret){
		pr_err("%s fail\n",__func__);
		return ret;
	}
	
	return ret;
}
static int sgm4154x_set_input_volt_lim(struct charger_device *chg_dev, unsigned int vindpm)
{
	int ret;
	unsigned int offset;
	u8 reg_val;
	u8 os_val;
	struct sgm4154x_device *sgm = charger_get_data(chg_dev);
	
	if (vindpm < SGM4154x_VINDPM_V_MIN_uV ||
	    vindpm > SGM4154x_VINDPM_V_MAX_uV)
 		return -EINVAL;	
	
	if (vindpm < 5900000){
		os_val = 0;
		offset = 3900000;
	}		
	else if (vindpm >= 5900000 && vindpm < 7500000){
		os_val = 1;
		offset = 5900000; //uv
	}		
	else if (vindpm >= 7500000 && vindpm < 10500000){
		os_val = 2;
		offset = 7500000; //uv
	}		
	else{
		os_val = 3;
		offset = 10500000; //uv
	}		
	
	sgm4154x_set_vindpm_offset_os(sgm,os_val);
	reg_val = (vindpm - offset) / SGM4154x_VINDPM_STEP_uV;	

	ret = sgm4154x_update_bits(sgm, SGM4154x_CHRG_CTRL_6,
				  SGM4154x_VINDPM_V_MASK, reg_val); 

	return ret;
}

#if(0)
static int sgm4154x_get_input_volt_lim(struct charger_device *chg_dev, u32 *uV)
{
	int ret;
	int offset;
	u8 vlim;
	int temp;
	
	struct sgm4154x_device *sgm = charger_get_data(chg_dev);
	ret = sgm4154x_read_reg(sgm, SGM4154x_CHRG_CTRL_6, &vlim);
	if (ret)
		return ret;
	
	temp = sgm4154x_get_vindpm_offset_os(sgm);
	if (0 == temp)
		offset = 3900000; //uv
	else if (1 == temp)
		offset = 5900000;
	else if (2 == temp)
		offset = 7500000;
	else if (3 == temp)
		offset = 10500000;
	else
		return temp;
	
	*uV = offset + (vlim & 0x0F) * SGM4154x_VINDPM_STEP_uV;
	
	return 0;
}
#endif

static int sgm4154x_get_input_minvolt_lim(struct charger_device *chg_dev, bool *uV)
{	
	struct sgm4154x_device *sgm = charger_get_data(chg_dev);
	u8 reg_val = 0;
	int ret = 0;

	ret = sgm4154x_read_reg(sgm, SGM4154x_CHRG_CTRL_a, &reg_val);
	*uV = ((reg_val >> 6) & 0x01);

	return ret;
}

static int sgm4154x_get_vbus(int *vchr)
{
	static struct mtk_charger *pinfo;
	struct power_supply *psy;
	int ret = 0;

	if (pinfo == NULL) {
		psy = power_supply_get_by_name("mtk-master-charger");
		if (psy == NULL) {
			chr_err("[%s]psy is not rdy\n", __func__);
			return -1;
		}

		pinfo = (struct mtk_charger *)power_supply_get_drvdata(psy);
		if (pinfo == NULL) {
			chr_err("[%s]mtk_gauge is not rdy\n", __func__);
			return -1;
		}
	}

	if (!IS_ERR_OR_NULL(pinfo->data.chan_vbus)) {
		ret = iio_read_channel_processed(pinfo->data.chan_vbus, vchr);
	}

	*vchr = *vchr  * 10;
	chr_debug("%s vbus:%d\n", __func__, * vchr);
	return ret;
}

static int sgm4154x_set_input_curr_lim(struct charger_device *chg_dev, unsigned int iindpm)
{
	int ret;
	u8 reg_val;
	struct sgm4154x_device *sgm = charger_get_data(chg_dev);

	if (iindpm < SGM4154x_IINDPM_I_MIN_uA ||
			iindpm > SGM4154x_IINDPM_I_MAX_uA)
		return -EINVAL;

	pr_err("%s: set_input_curr_lim = %duA\n", __func__, iindpm);

#if (defined(__SGM41513_CHIP_ID__) || defined(__SGM41513A_CHIP_ID__) || defined(__SGM41513D_CHIP_ID__))
	reg_val = (iindpm-SGM4154x_IINDPM_I_MIN_uA) / SGM4154x_IINDPM_STEP_uA;
#else		
	if (iindpm >= SGM4154x_IINDPM_I_MIN_uA && iindpm <= 3100000)//default
		reg_val = (iindpm-SGM4154x_IINDPM_I_MIN_uA) / SGM4154x_IINDPM_STEP_uA;
	else if (iindpm > 3100000 && iindpm < SGM4154x_IINDPM_I_MAX_uA)
		reg_val = 0x1E;
	else
		reg_val = SGM4154x_IINDPM_I_MASK;
#endif
	ret = sgm4154x_update_bits(sgm, SGM4154x_CHRG_CTRL_0,
				  SGM4154x_IINDPM_I_MASK, reg_val);
	return ret;
}

static int sgm4154x_get_input_curr_lim(struct charger_device *chg_dev,unsigned int *ilim)
{
	int ret;	
	u8 reg_val;
	struct sgm4154x_device *sgm = charger_get_data(chg_dev);
	
	ret = sgm4154x_read_reg(sgm, SGM4154x_CHRG_CTRL_0, &reg_val);
	if (ret)
		return ret;	
	if (SGM4154x_IINDPM_I_MASK == (reg_val & SGM4154x_IINDPM_I_MASK))
		*ilim =  SGM4154x_IINDPM_I_MAX_uA;
	else
		*ilim = (reg_val & SGM4154x_IINDPM_I_MASK)*SGM4154x_IINDPM_STEP_uA + SGM4154x_IINDPM_I_MIN_uA;

	return 0;
}

static int sgm4154x_get_input_mincurr_lim(struct charger_device *chg_dev,u32 *ilim)
{
	
	*ilim = SGM4154x_IINDPM_I_MIN_uA;

	return 0;
}

static int sgm4154x_get_state(struct sgm4154x_device *sgm,
			     struct sgm4154x_state *state)
{
	u8 chrg_stat;
	u8 fault;
	u8 chrg_param_0,chrg_param_1,chrg_param_2;
	int ret;

	ret = sgm4154x_read_reg(sgm, SGM4154x_CHRG_STAT, &chrg_stat);
	if (ret){
		ret = sgm4154x_read_reg(sgm, SGM4154x_CHRG_STAT, &chrg_stat);
		if (ret){
			pr_err("%s read SGM4154x_CHRG_STAT fail\n",__func__);
			return ret;
		}
	}
	state->chrg_type = chrg_stat & SGM4154x_VBUS_STAT_MASK;
	state->chrg_stat = chrg_stat & SGM4154x_CHG_STAT_MASK;
	state->online = !!(chrg_stat & SGM4154x_PG_STAT);
	state->therm_stat = !!(chrg_stat & SGM4154x_THERM_STAT);
	state->vsys_stat = !!(chrg_stat & SGM4154x_VSYS_STAT);
	
	pr_err("%s chrg_type =%d,chrg_stat =%d online = %d\n",__func__,state->chrg_type,state->chrg_stat,state->online);
	

	ret = sgm4154x_read_reg(sgm, SGM4154x_CHRG_FAULT, &fault);
	if (ret){
		pr_err("%s read SGM4154x_CHRG_FAULT fail\n",__func__);
		return ret;
	}
	state->chrg_fault = fault;	
	state->ntc_fault = fault & SGM4154x_TEMP_MASK;
	state->health = state->ntc_fault;
	ret = sgm4154x_read_reg(sgm, SGM4154x_CHRG_CTRL_0, &chrg_param_0);
	if (ret){
		pr_err("%s read SGM4154x_CHRG_CTRL_0 fail\n",__func__);
		return ret;
	}
	state->hiz_en = !!(chrg_param_0 & SGM4154x_HIZ_EN);
	
	ret = sgm4154x_read_reg(sgm, SGM4154x_CHRG_CTRL_5, &chrg_param_1);
	if (ret){
		pr_err("%s read SGM4154x_CHRG_CTRL_5 fail\n",__func__);
		return ret;
	}
	state->term_en = !!(chrg_param_1 & SGM4154x_TERM_EN);
	
	ret = sgm4154x_read_reg(sgm, SGM4154x_CHRG_CTRL_a, &chrg_param_2);
	if (ret){
		pr_err("%s read SGM4154x_CHRG_CTRL_a fail\n",__func__);
		return ret;
	}
	state->vbus_gd = !!(chrg_param_2 & SGM4154x_VBUS_GOOD);

	return 0;
}

static int sgm4154x_get_hiz_mode(struct sgm4154x_device *sgm, u8 *state)
{
	u8 val = 0;
	int ret = 0;

	ret = sgm4154x_read_reg(sgm, SGM4154x_CHRG_CTRL_0, &val);
	if (ret) {
		pr_err("read SGM4154X_REG_00 failed, ret:%d\n", ret);
		return ret;
	}
	*state = (val & SGM4154X_ENHIZ_MASK) >> SGM4154X_ENHIZ_SHIFT;

	return 0;
}

static int sgm4154x_is_enable_hiz(struct charger_device *chg_dev, bool *en)
{
	u8 state = 0;
	struct sgm4154x_device *sgm = charger_get_data(chg_dev);
	if(IS_ERR_OR_NULL(sgm))
		return -ENOMEM;
	sgm4154x_get_hiz_mode(sgm, &state);
	*en = (bool)state;
	/*Barley code for OBARLEY-12509 by liyw37 at 2023.10.18 start*/
	pr_err("sgm4154x_is_enable_hiz,en = %d\n",*en);
	/*Barley code for OBARLEY-12509 by liyw37 at 2023.10.18 end*/

	return 0;
}

static int sgm4154x_set_hiz_en(struct charger_device *chg_dev, bool hiz_en)
{
	u8 reg_val;
	struct sgm4154x_device *sgm = charger_get_data(chg_dev);

	pr_err("%s:%d", __func__, hiz_en);
	reg_val = hiz_en ? SGM4154x_HIZ_EN : 0;

	return sgm4154x_update_bits(sgm, SGM4154x_CHRG_CTRL_0,
				  SGM4154x_HIZ_EN, reg_val);
}

static int sgm4154x_enable_charger(struct sgm4154x_device *sgm)
{
    int ret;
 
    ret = sgm4154x_update_bits(sgm, SGM4154x_CHRG_CTRL_1, SGM4154x_CHRG_EN,
                     SGM4154x_CHRG_EN);
	
    return ret;
}

static int sgm4154x_disable_charger(struct sgm4154x_device *sgm)
{
    int ret;
    
    ret = sgm4154x_update_bits(sgm, SGM4154x_CHRG_CTRL_1, SGM4154x_CHRG_EN,
                     0);
    return ret;
}

static int sgm4154x_is_charging(struct charger_device *chg_dev,bool *en)
{
	int ret;
	u8 val;
	struct sgm4154x_device *sgm = charger_get_data(chg_dev);
	
	ret = sgm4154x_read_reg(sgm, SGM4154x_CHRG_CTRL_1, &val);
	if (ret){
		pr_err("%s read SGM4154x_CHRG_CTRL_a fail\n",__func__);
		return ret;
	}
	*en = (val&SGM4154x_CHRG_EN)? 1 : 0;
	return ret;
}

static int sgm4154x_charging_switch(struct charger_device *chg_dev,bool enable)
{
	int ret;
	struct sgm4154x_device *sgm = charger_get_data(chg_dev);
	
	if (enable)
		ret = sgm4154x_enable_charger(sgm);
	else
		ret = sgm4154x_disable_charger(sgm);
	return ret;
}

static int sgm4154x_set_recharge_volt(struct sgm4154x_device *sgm, int mV)
{
	u8 reg_val;
	
	reg_val = (mV - SGM4154x_VRECHRG_OFFSET_mV) / SGM4154x_VRECHRG_STEP_mV;

	return sgm4154x_update_bits(sgm, SGM4154x_CHRG_CTRL_4,
				  SGM4154x_VRECHARGE, reg_val);
}

static int sgm4154x_set_wdt_rst(struct sgm4154x_device *sgm, bool is_rst)
{
	u8 val;
	
	if (is_rst)
		val = SGM4154x_WDT_RST_MASK;
	else
		val = 0;
	return sgm4154x_update_bits(sgm, SGM4154x_CHRG_CTRL_1,
				  SGM4154x_WDT_RST_MASK, val);	
}

/**********************************************************
 *
 *   [Internal Function]
 *
 *********************************************************/
static int sgm4154x_dump_register(struct charger_device *chg_dev)
{

	unsigned char i = 0;
	unsigned int ret = 0;
	unsigned char sgm4154x_reg[SGM4154x_REG_NUM+1] = { 0 }; 
	struct sgm4154x_device *sgm = charger_get_data(chg_dev);
		
	for (i = 0; i < SGM4154x_REG_NUM+1; i++) {
		ret = sgm4154x_read_reg(sgm,i, &sgm4154x_reg[i]);
		if (ret != 0) {
			pr_info("[sgm4154x] i2c transfor error\n");
			return 1;
		}
		pr_info("%s,[0x%x]=0x%x ",__func__, i, sgm4154x_reg[i]);
	}
	
	return 0;
}


/**********************************************************
 *
 *   [Internal Function]
 *
 *********************************************************/
static int sgm4154x_hw_chipid_detect(struct sgm4154x_device *sgm)
{
	int ret = 0;
	u8 val = 0;
	ret = sgm4154x_read_reg(sgm,SGM4154x_CHRG_CTRL_b,&val);
	if (ret < 0)
	{
		pr_info("[%s] read SGM4154x_CHRG_CTRL_b fail\n", __func__);
		return ret;
	}		
	val = val & SGM4154x_PN_MASK;
	pr_info("[%s] Reg[0x0B]=0x%x\n", __func__,val);
	
	return val;
}

static int sgm4154x_reset_watch_dog_timer(struct charger_device
		*chg_dev)
{
	int ret;
	struct sgm4154x_device *sgm = charger_get_data(chg_dev);

	pr_info("charging_reset_watch_dog_timer\n");

	ret = sgm4154x_set_wdt_rst(sgm,0x1);	/* RST watchdog */	

	return ret;
}


static int sgm4154x_get_charging_status(struct charger_device *chg_dev,
				       bool *is_done)
{
	struct sgm4154x_device *sgm = charger_get_data(chg_dev);
	u8 chrg_stat;
	int ret = 0;

	ret = sgm4154x_read_reg(sgm, SGM4154x_CHRG_STAT, &chrg_stat);
	if (ret){
		ret = sgm4154x_read_reg(sgm, SGM4154x_CHRG_STAT, &chrg_stat);
		if (ret){
			*is_done = false;
			pr_err("%s read SGM4154x_CHRG_STAT fail\n",__func__);
			return ret;
		}
	}

	if ((chrg_stat & SGM4154x_CHG_STAT_MASK) == SGM4154x_TERM_CHRG)
		*is_done = true;
	else
		*is_done = false;

	return 0;
}

static int sgm4154x_set_vindpm_track(struct sgm4154x_device *sgm,
						enum vindpm_track track)
{
	int ret;

	ret = sgm4154x_update_bits(sgm, SGM4154x_CHRG_CTRL_7,
				SGM4154x_VINDPM_TRACK_MASK, track);

	if (ret < 0)
		pr_err("[%s] set vindpm_track fail\n",__func__);

	return ret;
}

static int sgm4154x_set_en_timer(struct sgm4154x_device *sgm)
{
	int ret;	

	ret = sgm4154x_update_bits(sgm, SGM4154x_CHRG_CTRL_5,
				SGM4154x_SAFETY_TIMER_EN, SGM4154x_SAFETY_TIMER_EN);

	return ret;
}

static int sgm4154x_set_disable_timer(struct sgm4154x_device *sgm)
{
	int ret;	

	ret = sgm4154x_update_bits(sgm, SGM4154x_CHRG_CTRL_5,
				SGM4154x_SAFETY_TIMER_EN, 0);

	return ret;
}

static int sgm4154x_enable_safetytimer(struct charger_device *chg_dev,bool en)
{
	struct sgm4154x_device *sgm = charger_get_data(chg_dev);
	int ret = 0;

	if (en)
		ret = sgm4154x_set_en_timer(sgm);
	else
		ret = sgm4154x_set_disable_timer(sgm);
	return ret;
}

static int sgm4154x_get_is_safetytimer_enable(struct charger_device
		*chg_dev,bool *en)
{
	int ret = 0;
	u8 val = 0;
	
	struct sgm4154x_device *sgm = charger_get_data(chg_dev);
	
	ret = sgm4154x_read_reg(sgm,SGM4154x_CHRG_CTRL_5,&val);
	if (ret < 0)
	{
		pr_info("[%s] read SGM4154x_CHRG_CTRL_5 fail\n", __func__);
		return ret;
	}
	*en = !!(val & SGM4154x_SAFETY_TIMER_EN);
	return 0;
}

#if (defined(__SGM41542_CHIP_ID__)|| defined(__SGM41516D_CHIP_ID__)|| defined(__SGM41543D_CHIP_ID__))
static int sgm4154x_en_pe_current_partern(struct charger_device
		*chg_dev,bool is_up)
{
	int ret = 0;	
	
	struct sgm4154x_device *sgm = charger_get_data(chg_dev);
	
	ret = sgm4154x_update_bits(sgm, SGM4154x_CHRG_CTRL_d,
				SGM4154x_EN_PUMPX, SGM4154x_EN_PUMPX);
	if (ret < 0)
	{
		pr_info("[%s] read SGM4154x_CHRG_CTRL_d fail\n", __func__);
		return ret;
	}
	if (is_up)
		ret = sgm4154x_update_bits(sgm, SGM4154x_CHRG_CTRL_d,
				SGM4154x_PUMPX_UP, SGM4154x_PUMPX_UP);
	else
		ret = sgm4154x_update_bits(sgm, SGM4154x_CHRG_CTRL_d,
				SGM4154x_PUMPX_DN, SGM4154x_PUMPX_DN);
	return ret;
}
#endif

static enum power_supply_property sgm4154x_power_supply_props[] = {
	POWER_SUPPLY_PROP_MANUFACTURER,
	POWER_SUPPLY_PROP_MODEL_NAME,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
	POWER_SUPPLY_PROP_CHARGE_TYPE,
	POWER_SUPPLY_PROP_USB_TYPE,
	//POWER_SUPPLY_PROP_CHARGING_ENABLED,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_TYPE
};

static int sgm4154x_property_is_writeable(struct power_supply *psy,
					 enum power_supply_property prop)
{
	switch (prop) {
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
	case POWER_SUPPLY_PROP_PRECHARGE_CURRENT:
	case POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT:
	//case POWER_SUPPLY_PROP_CHARGING_ENABLED:
		return true;
	default:
		return false;
	}
}
static int sgm4154x_charger_set_property(struct power_supply *psy,
		enum power_supply_property prop,
		const union power_supply_propval *val)
{
	//struct sgm4154x_device *sgm = power_supply_get_drvdata(psy);
	int ret = -EINVAL;

	switch (prop) {
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		ret = sgm4154x_set_input_curr_lim(s_chg_dev_otg, val->intval);
		break;
/*	case POWER_SUPPLY_PROP_CHARGING_ENABLED:
		sgm4154x_charging_switch(s_chg_dev_otg,val->intval);		
		break;
	case POWER_SUPPLY_PROP_INPUT_VOLTAGE_LIMIT:
		ret = sgm4154x_set_input_volt_lim(s_chg_dev_otg, val->intval);
		break;*/
	default:
		return -EINVAL;
	}

	return ret;
}

static int sgm4154x_charger_get_property(struct power_supply *psy,
				enum power_supply_property psp,
				union power_supply_propval *val)
{
	struct sgm4154x_device *sgm = power_supply_get_drvdata(psy);
	struct sgm4154x_state state;
	int ret = 0;

	if (!sgm) {
		pr_info( "%s:line%d: NULL pointer!!!\n", __func__, __LINE__);
		return -EINVAL;
	}

	mutex_lock(&sgm->lock);
	//ret = sgm4154x_get_state(sgm, &state);
	state = sgm->state;
	mutex_unlock(&sgm->lock);
	if (ret)
		return ret;

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		if (!state.chrg_type || (state.chrg_type == SGM4154x_OTG_MODE))
			val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
		else if (sgm->chg_done)
			val->intval = POWER_SUPPLY_STATUS_FULL;
		else if (!state.chrg_stat || state.chrg_stat == 24)
			val->intval = POWER_SUPPLY_STATUS_NOT_CHARGING;
		else
			val->intval = POWER_SUPPLY_STATUS_CHARGING;
		break;
	case POWER_SUPPLY_PROP_CHARGE_TYPE:
		switch (state.chrg_stat) {		
		case SGM4154x_PRECHRG:
			val->intval = POWER_SUPPLY_CHARGE_TYPE_TRICKLE;
			break;
		case SGM4154x_FAST_CHRG:
			val->intval = POWER_SUPPLY_CHARGE_TYPE_FAST;
			break;		
		case SGM4154x_TERM_CHRG:
			val->intval = POWER_SUPPLY_CHARGE_TYPE_TRICKLE;
			break;
		case SGM4154x_NOT_CHRGING:
			val->intval = POWER_SUPPLY_CHARGE_TYPE_NONE;
			break;
		default:
			val->intval = POWER_SUPPLY_CHARGE_TYPE_UNKNOWN;
		}
		break;
	case POWER_SUPPLY_PROP_MANUFACTURER:
		val->strval = SGM4154x_MANUFACTURER;
		break;

	case POWER_SUPPLY_PROP_MODEL_NAME:
		val->strval = SGM4154x_NAME;
		break;

	case POWER_SUPPLY_PROP_ONLINE:
               val->intval = state.online;
	        /* Force to 1 in all charger type */           
		break;
	case POWER_SUPPLY_PROP_USB_TYPE:
		val->intval = sgm->psy_usb_type;
		if(sgm->pd_adapter == POWER_SUPPLY_USB_TYPE_PD &&
			sgm->state.chrg_type != SGM4154x_USB_CDP)
			val->intval = sgm->pd_adapter;
		if(sgm->is_hvdcp_type)
			val->intval = POWER_SUPPLY_USB_TYPE_C;
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = state.vbus_gd;
		break;
	case POWER_SUPPLY_PROP_TYPE:
		switch(sgm->state.chrg_type) {
			case SGM4154x_USB_SDP:
				val->intval = POWER_SUPPLY_TYPE_USB;
				if(sgm->pd_adapter == POWER_SUPPLY_USB_TYPE_PD)
					val->intval = POWER_SUPPLY_TYPE_USB_DCP;
			break;

			case SGM4154x_USB_CDP:
				val->intval = POWER_SUPPLY_TYPE_USB_CDP;
			break;

			case SGM4154x_USB_DCP:
				val->intval = POWER_SUPPLY_TYPE_USB_DCP;
			break;

			case SGM4154x_UNKNOWN:
				val->intval = POWER_SUPPLY_TYPE_USB_DCP;
			break;	

			default:
				val->intval = POWER_SUPPLY_TYPE_UNKNOWN;
			break;
		}
		break;	

	case POWER_SUPPLY_PROP_HEALTH:
		if (state.chrg_fault & 0xF8)
			val->intval = POWER_SUPPLY_HEALTH_OVERVOLTAGE;
		else
			val->intval = POWER_SUPPLY_HEALTH_GOOD;

		switch (state.health) {
		case SGM4154x_TEMP_HOT:
			val->intval = POWER_SUPPLY_HEALTH_OVERHEAT;
			break;
		case SGM4154x_TEMP_WARM:
			val->intval = POWER_SUPPLY_HEALTH_GOOD;
			break;
		case SGM4154x_TEMP_COOL:
			val->intval = POWER_SUPPLY_HEALTH_GOOD;
			break;
		case SGM4154x_TEMP_COLD:
			val->intval = POWER_SUPPLY_HEALTH_COLD;
			break;
		}
		break;

	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		//val->intval = state.vbus_adc;
		break;

	case POWER_SUPPLY_PROP_CURRENT_NOW:
		//val->intval = state.ibus_adc;
		break;

/*	case POWER_SUPPLY_PROP_INPUT_VOLTAGE_LIMIT:
		ret = sgm4154x_get_input_volt_lim(sgm);
		if (ret < 0)
			return ret;

		val->intval = ret;
		break;*/

	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:		
		break;
#if 0
	case POWER_SUPPLY_PROP_CHARGING_ENABLED:
		val->intval = !state.hiz_en;
		break;
#endif
	default:
		return -EINVAL;
	}

	return ret;
}

static int sgm4154x_get_property(struct charger_device *chg_dev,
			    enum charger_property prop,
			    union charger_propval *val) {
	struct sgm4154x_device *sgm = dev_get_drvdata(&chg_dev->dev);
	int ret = 0;
	struct sgm4154x_state state;

	if(sgm == NULL)
		return -EINVAL;

	mutex_lock(&sgm->lock);
	state = sgm->state;
	mutex_unlock(&sgm->lock);

	switch (prop) {
	case CHARGER_PROP_ONLINE:
		val->intval = state.online;
	        /* Force to 1 in all charger type */
	break;
	case CHARGER_PROP_STATUS:
		if (!state.chrg_type || (state.chrg_type == SGM4154x_OTG_MODE))
			val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
		else if (!state.chrg_stat)
			val->intval = POWER_SUPPLY_STATUS_NOT_CHARGING;
		else if (sgm->chg_done)
			val->intval = POWER_SUPPLY_STATUS_FULL;
		else
			val->intval = POWER_SUPPLY_STATUS_CHARGING;
		break;
    case CHARGER_PROP_USB_TYPE:
		val->intval = sgm->psy_usb_type;
		if(sgm->pd_adapter == POWER_SUPPLY_USB_TYPE_PD &&
			sgm->state.chrg_type != SGM4154x_USB_CDP)
			val->intval = sgm->pd_adapter;
		if(sgm->is_hvdcp_type)
			val->intval = POWER_SUPPLY_USB_TYPE_C;
	break;
	case CHARGER_PROP_TYPE:
		switch(sgm->state.chrg_type) {
			case SGM4154x_USB_SDP:
				val->intval = POWER_SUPPLY_TYPE_USB;
				if(sgm->pd_adapter == POWER_SUPPLY_USB_TYPE_PD)
					val->intval = POWER_SUPPLY_TYPE_USB_DCP;
			break;

			case SGM4154x_USB_CDP:
				val->intval = POWER_SUPPLY_TYPE_USB_CDP;
			break;

			case SGM4154x_USB_DCP:
				val->intval = POWER_SUPPLY_TYPE_USB_DCP;
			break;

			case SGM4154x_UNKNOWN:
				val->intval = POWER_SUPPLY_TYPE_USB_DCP;
			break;

			case SGM4154x_NON_STANDARD:
				val->intval = POWER_SUPPLY_TYPE_USB_DCP;
			break;
			default:
				val->intval = POWER_SUPPLY_TYPE_UNKNOWN;
			break;
		}
	break;
	case CHARGER_PROP_ADP_TYPE:
		if(sgm->is_nonstand_adapter)
			val->intval = sgm->adp_type;
		else
			val->intval = NORMAL_ADAPTER;
	break;
    default:
        ret = -EINVAL;
	break;
    }
    return ret;
}

static int32_t sgm4154x_set_dpdm(
	struct sgm4154x_device *sgm, uint8_t dp_val, uint8_t dm_val)
{
	uint8_t data_reg = 0;

	uint8_t mask = SGM4154x_DP_VSEL_MASK|SGM4154x_DM_VSEL_MASK;

	data_reg  = (dp_val & SGM4154x_DP_VSEL_MASK) << SGM4154x_DP_VOLT_SHIFT;
	data_reg |= (dm_val & SGM4154x_DM_VSEL_MASK) << SGM4154x_DM_VOLT_SHIFT;

	return sgm4154x_update_bits(sgm, SGM4154x_CHRG_CTRL_d,
				  mask, data_reg);

}

static int sgm4154x_enable_qc20_hvdcp_9v(struct sgm4154x_device *sgm)
{
	int ret;
	uint8_t dp_val, dm_val;

	//dp and dm connected,dp 0.6V dm 0.6V
	dp_val = 0x2<<3;
	ret = sgm4154x_update_bits(sgm, SGM4154x_CHRG_CTRL_d,SGM4154x_DP_VSEL_MASK, dp_val); //dp 0.6V
	if (ret){
        return ret;
	}
	dm_val = 0;
	ret = sgm4154x_update_bits(sgm, SGM4154x_CHRG_CTRL_d,
				  SGM4154x_DM_VSEL_MASK, dm_val); //dm 0V
	if (ret){
		return ret;
	}
	mdelay(1400);

	// dp 3.3v and dm 0.6v out 9V
	dp_val = SGM4154x_DP_VSEL_MASK;
	ret = sgm4154x_update_bits(sgm, SGM4154x_CHRG_CTRL_d,
				  SGM4154x_DP_VSEL_MASK, dp_val); //dp 3.3v
	if (ret)
		return ret;

	dm_val = 0x2<<1;
	ret = sgm4154x_update_bits(sgm, SGM4154x_CHRG_CTRL_d,
				  SGM4154x_DM_VSEL_MASK, dm_val); //dm 0.6v

	return ret;
}

static int sgm4154x_charging_set_hvdcp20(
		struct sgm4154x_device *sgm, uint32_t vbus_target)
{
	int32_t ret = 0;

	pr_err("Set vbus target %dv\n", vbus_target);
	switch (vbus_target) {
	case 5:
		ret = sgm4154x_set_dpdm(sgm, SGM_DP_DM_VOL_0P6, SGM_DP_DM_VOL_0P0);
		if(ret < 0) {
			msleep(100);
			ret = sgm4154x_set_dpdm(sgm, SGM_DP_DM_VOL_0P6, SGM_DP_DM_VOL_0P0);
		}
		break;
	case 9:
		ret = sgm4154x_enable_qc20_hvdcp_9v(sgm);
		break;
	case 12:
		ret = sgm4154x_set_dpdm(sgm, SGM_DP_DM_VOL_0P6, SGM_DP_DM_VOL_0P6);
		break;
	default:
		ret = sgm4154x_set_dpdm(sgm, SGM_DP_DM_VOL_HIZ, SGM_DP_DM_VOL_HIZ);
		break;
	}

	return (ret < 0) ? ret : 0;
}

static void sgm4154x_hvdcp_ready(struct sgm4154x_device * sgm, bool en)
{
	sgm->is_hvdcp_type = en;

	return;
}

static void sgm4154x_chg_type_hvdcp_work(struct work_struct *work)
{
	struct sgm4154x_device * sgm =
		container_of(work, struct sgm4154x_device, hvdcp_work.work);
	int vbus;
	int ret;
	int retry = 3;
	if(sgm == NULL) {
		pr_err("Cann't get sgm \n");
		return ;
	}

	if(sgm->pd_adapter == POWER_SUPPLY_USB_TYPE_PD) {
		sgm4154x_set_dpdm(sgm, SGM_DP_DM_VOL_HIZ, SGM_DP_DM_VOL_HIZ);
		sgm->hvdcp_detect_count = 0;
		return;
	}
	if (sgm->hvdcp_detect_count == 0 && !sgm->is_hvdcp_type && !sgm->hvdcp_disable) {
		sgm4154x_set_dpdm(sgm, SGM_DP_DM_VOL_0P6, SGM_DP_DM_VOL_HIZ);
		sgm->hvdcp_detect_time = cpu_clock(smp_processor_id()) / CPU_CLOCK_TIME_MS;
		sgm4154x_charging_set_hvdcp20(sgm,9);
		mdelay(100);
	}
	if (!sgm->state.online) {
		sgm->hvdcp_detect_count = 0;
		return;
	}
	if(sgm->is_hvdcp_type) {
		sgm->hvdcp_detect_count = 0;
		return;
	}

	ret = sgm4154x_get_vbus(&vbus);
	if (ret < 0)
		dev_err(sgm->dev,"%s: %d\n", __func__, ret);

	if(sgm->pd_adapter == POWER_SUPPLY_USB_TYPE_PD)
		return;

	pr_err("%s HVDCP,vbus =%d, count = %d",__func__,vbus, sgm->hvdcp_detect_count);

	if(vbus > HVDCP_VBUS)
	{
		sgm4154x_hvdcp_ready(sgm, true);
		sgm->hvdcp_detect_count = 0;
		power_supply_changed(sgm->charger);
		while(retry --) {
			sgm4154x_charging_set_hvdcp20(sgm,5);
			mdelay(100);
			ret = sgm4154x_get_vbus(&vbus);
			pr_err("%s set 5v,vbus =%d, retry = %d",__func__,vbus, retry);
			if(vbus > HVDCP_VBUS)
				mdelay(100);
			else
				break;
		}
	} else if(sgm->hvdcp_disable) {
		sgm4154x_hvdcp_ready(sgm, true);
		pr_err("%s hvdcp_disable is %d, ",__func__,sgm->hvdcp_disable);
	} else {
		sgm4154x_hvdcp_ready(sgm, false);
		sgm->hvdcp_detect_count++;
		if(sgm->hvdcp_detect_count <= HVDCP_DETECT_COUNT ){
			schedule_delayed_work(&sgm->hvdcp_work, msecs_to_jiffies(2000));
		} else {
			sgm->hvdcp_detect_count = 0;
		}
	}

}

#if 0
static bool sgm4154x_state_changed(struct sgm4154x_device *sgm,
				  struct sgm4154x_state *new_state)
{
	struct sgm4154x_state old_state;

	mutex_lock(&sgm->lock);
	old_state = sgm->state;
	mutex_unlock(&sgm->lock);

	return (old_state.chrg_type != new_state->chrg_type ||
		old_state.chrg_stat != new_state->chrg_stat     ||		
		old_state.online != new_state->online		    ||
		old_state.therm_stat != new_state->therm_stat	||		
		old_state.vsys_stat != new_state->vsys_stat 	||
		old_state.chrg_fault != new_state->chrg_fault	
		);
}
#endif

static bool sgm4154x_dpdm_detect_is_done(struct sgm4154x_device * sgm)
{
	u8 chrg_stat;
	int ret;

	ret = sgm4154x_read_reg(sgm, SGM4154x_INPUT_DET, &chrg_stat);
	if(ret) {
		dev_err(sgm->dev, "Check DPDM detecte error\n");
	}


	return (chrg_stat&SGM4154x_DPDM_ONGOING)?true:false;
}

static void charger_monitor_work_func(struct work_struct *work)
{
	int ret = 0;
	struct sgm4154x_device * sgm = NULL;
	struct delayed_work *charge_monitor_work = NULL;
	//static u8 last_chg_method = 0;
	struct sgm4154x_state state;
	int vbus = 0;

	charge_monitor_work = container_of(work, struct delayed_work, work);
	if(charge_monitor_work == NULL) {
		pr_err("Cann't get charge_monitor_work\n");
		return ;
	}
	sgm = container_of(charge_monitor_work, struct sgm4154x_device, charge_monitor_work);
	if(sgm == NULL) {
		pr_err("Cann't get sgm \n");
		return ;
	}

	if(sgm->is_suspend) {
		dev_err(sgm->dev, "system suspend, not get charger state\n");
		goto OUT;
	}

	if(sgm->pd_adapter == POWER_SUPPLY_USB_TYPE_PD)
		return;

	ret = sgm4154x_get_state(sgm, &state);
	if (ret < 0)
		dev_err(sgm->dev,"%s: %d\n", __func__, ret);
	mutex_lock(&sgm->lock);
	sgm->state = state;
	mutex_unlock(&sgm->lock);

	if(!sgm->state.vbus_gd) {
		dev_err(sgm->dev, "Vbus not present, disable charge\n");
		sgm4154x_disable_charger(sgm);
		goto OUT;
	}
	if(!state.online)
	{
		dev_err(sgm->dev, "Vbus not online\n");
		goto OUT;
	}

	ret = sgm4154x_get_vbus(&vbus);
	if(vbus > HVDCP_VBUS) {
		msleep(1000);
		ret = sgm4154x_get_vbus(&vbus);
		if(vbus > HVDCP_VBUS) {
			cancel_delayed_work_sync(&sgm->hvdcp_work);
			if(!sgm->is_hvdcp_type) {
				sgm4154x_hvdcp_ready(sgm, true);
				power_supply_changed(sgm->charger);
			}
			sgm->hvdcp_detect_count = 0;
			sgm4154x_charging_set_hvdcp20(sgm,5);
			pr_err("%s vbus force 5v %d\n",__func__, vbus);
		}
	}
	pr_err("%s\n",__func__);
OUT:
	schedule_delayed_work(&sgm->charge_monitor_work, 10*HZ);
}

static int sgm4154x_force_dpdm(struct sgm4154x_device *sgm)
{
		Charger_Detect_Init();
		msleep(5);

	return sgm4154x_update_bits(sgm, SGM4154x_CHRG_CTRL_7, SGM4154X_FORCE_DPDM_MASK,
				   SGM4154X_FORCE_DPDM);
}

static irqreturn_t sgm4154x_irq_handler_thread(int irq, void *private);
static void sgm4154x_force_detection_dwork_handler(struct work_struct *work)
{
    int ret = 0;
    bool hiz_en = false;
    struct sgm4154x_device *sgm = container_of(work,
                                        struct sgm4154x_device,
                                        force_detect_dwork.work);

    if (!sgm->state.online)
        return;

    sgm->force_detect_count++;
    sgm->bc12_down = true;

    ret = sgm4154x_force_dpdm(sgm);
    if (ret) {
        dev_err(sgm->dev, "%s: force dpdm failed(%d)\n", __func__, ret);
        return;
    }

    msleep(1000);
	dev_err(sgm->dev, "%s: force dpdm\n", __func__);

	if(sgm->pd_adapter == POWER_SUPPLY_USB_TYPE_PD)
		Charger_Detect_Release();
    ret = sgm4154x_is_enable_hiz(sgm->chg_dev, &hiz_en);
    if (ret) {
        dev_err(sgm->dev, "%s: get hiz mode fail ret=%d\n", __func__, ret);
        return;
    }
    if (hiz_en) {
        dev_info(sgm->dev, "%s: hiz is enable,return\n", __func__);
        return;
    }

    sgm4154x_irq_handler_thread(sgm->client->irq, sgm);
}

static void sgm4151x_first_sdp_detection_dwork_handler(struct work_struct *work) {
    int ret;
    struct sgm4154x_device *sgm = container_of(work,
                                        struct sgm4154x_device,
                                        first_sdp_detect_dwork.work);

    if((sgm->state.vbus_gd) && (sgm->chg_type == POWER_SUPPLY_TYPE_USB)) {
        dev_err(sgm->dev, "%s: first sdp retry dpdm\n", __func__);
        ret = sgm4154x_force_dpdm(sgm);
        if (ret) {
            dev_err(sgm->dev, "%s: force dpdm failed(%d)\n", __func__, ret);
            return;
        }
		sgm->force_detect_count++;
	}

	msleep(500);

	if((sgm->state.vbus_gd) && sgm->chg_type == POWER_SUPPLY_TYPE_USB_CDP) {
        msleep(8000);
        if(sgm->state.vbus_gd && sgm->chg_type == POWER_SUPPLY_TYPE_USB) {
            dev_err(sgm->dev, "%s: first cdp retry dpdm\n", __func__);
            ret = sgm4154x_force_dpdm(sgm);
            if (ret) {
                dev_err(sgm->dev, "%s: force dpdm failed(%d)\n", __func__, ret);
                return;
            }
            sgm->force_detect_count++;
        }
    }
}

static int get_nonstand_charge_type(struct sgm4154x_device * sgm )
{
	int curr_lim = 0, ret;

	if(sgm->adp_type == NONSTAND_APPLE_12W
		|| sgm->adp_type == NONSTAND_SAMSUNG_10W
		|| sgm->adp_type == NONSTAND_APPLE_10W
		|| sgm->adp_type == NONSTAND_APPLE_5W
		|| sgm->adp_type == NONSTAND_DEFAULT)
	return sgm->adp_type;

	ret = sgm4154x_get_input_curr_lim(sgm->chg_dev, &curr_lim);
	if(ret == 0){
		curr_lim /= 1000; //ua to ma
		pr_info("charger type: NON_STANDARD curr_lim:%dma\n", curr_lim);
		switch (curr_lim) {
			case NONSTAND_APPLE_12W:
			case NONSTAND_SAMSUNG_10W:
			case NONSTAND_APPLE_10W:
			case NONSTAND_APPLE_5W:
				return curr_lim;
			default:
				if(sgm->is_nonstand_adapter)
					return NONSTAND_DEFAULT;
				else
					return NORMAL_ADAPTER;
		}
	}

	return NORMAL_ADAPTER;
}

static void chg_wakelock(struct sgm4154x_device * sgm, bool awake)
{
	static bool pm_flag = false;
	if(!sgm || !sgm->charger_wakelock)
		return;

	if (!pm_flag && awake) {
		pr_info("%s true\n",__func__);
		pm_flag = true;
		__pm_stay_awake(sgm->charger_wakelock);
    } else if(pm_flag && !awake){
		pr_info("%s release\n",__func__);
		pm_flag = false;
		__pm_relax(sgm->charger_wakelock);
	}
}

static irqreturn_t sgm4154x_irq_handler_thread(int irq, void *private)
{
	struct sgm4154x_device * sgm = private;
	static int vbus_gd = 0;
	bool pre_vbus_gd = false;
	struct sgm4154x_state state = { };
	int ret = 0;
	int chg_stat = 0;
	int pre_chg_stat = 0;

	pr_info("%s entry\n",__func__);

	ret = sgm4154x_get_state(sgm, &state);
	if(ret) {
		ret = sgm4154x_get_state(sgm, &state);
		if(ret)
			dev_err(sgm->dev, "sgm4154x_get_state failed\n");
	}
	pre_chg_stat = sgm->state.chrg_stat;
	mutex_lock(&sgm->lock);
	sgm->state = state;
	mutex_unlock(&sgm->lock);

	chg_stat = sgm->state.chrg_stat;
	pre_vbus_gd = vbus_gd;
	vbus_gd = sgm->state.online;
	if (pre_chg_stat > 0 && pre_chg_stat != SGM4154x_TERM_CHRG
		&& chg_stat == SGM4154x_TERM_CHRG) {
		pr_info("%s: pre_chg_stat=%d,chg_stat=%d,charging done\n",__func__, pre_chg_stat, chg_stat);
		return IRQ_HANDLED;
	}
	if (!pre_vbus_gd && vbus_gd) {
		dev_err(sgm->dev, "adapter/usb inserted \n");
		chg_wakelock(sgm, true);
		//Charger_Detect_Init();
		sgm->force_detect_count = 0;
		sgm->float_count = 0;
		sgm->chg_done = false;
		sgm->hvdcp_detect_count = 0;
		//sgm->float_to_sdp_count = 0;
		sgm->is_nonstand_adapter = false;
		sgm4154x_set_ichrg_curr(sgm->chg_dev,500000);
		sgm4154x_set_vindpm_track(sgm, SGM4154x_TRACK_250);
		schedule_delayed_work(&sgm->force_detect_dwork, msecs_to_jiffies(50));
		goto err;
	} else if(pre_vbus_gd && !vbus_gd) {
		dev_err(sgm->dev, "adapter/usb removed \n");
		sgm->psy_usb_type = POWER_SUPPLY_USB_TYPE_UNKNOWN;
		sgm->chg_type = POWER_SUPPLY_TYPE_UNKNOWN;
		sgm->hvdcp_detach_time = cpu_clock(smp_processor_id()) / CPU_CLOCK_TIME_MS;
		if(sgm->hvdcp_detach_time - sgm->hvdcp_detect_time <= HVDCP_DETECT_TO_DETACH_TIME)
			sgm->hvdcp_disable = true;
		else
			sgm->hvdcp_disable = false;
		pr_err("hvdcp_detach_time is %d, hvdcp_detect_time is %d\n", sgm->hvdcp_detach_time, sgm->hvdcp_detect_time);
		sgm->float_count = 0;
		sgm->chg_done = false;
		sgm->hvdcp_detect_count = 0;
		//sgm->float_to_sdp_count = 0;
		Charger_Detect_Release();
		sgm->non_std_rerun = false;
		sgm->bc12_down = false;
		sgm4154x_hvdcp_ready(sgm, false);
		sgm->is_nonstand_adapter = false;
		sgm4154x_update_bits(sgm, SGM4154x_CHRG_CTRL_0,
				  SGM4154x_HIZ_EN, 0);
		cancel_delayed_work_sync(&sgm->force_detect_dwork);
		cancel_delayed_work_sync(&sgm->hvdcp_work);
		chg_wakelock(sgm, false);
	}
	if(!sgm->state.vbus_gd) {
		dev_err(sgm->dev, "Vbus not present, disable charge\n");
		sgm->psy_usb_type = POWER_SUPPLY_USB_TYPE_UNKNOWN;
		sgm->chg_type = POWER_SUPPLY_TYPE_UNKNOWN;
		sgm->state.chrg_type = 0;
		sgm->float_count = 0;
		sgm->hvdcp_detect_count = 0;
		sgm4154x_disable_charger(sgm);
		goto err;
	}
	if(!state.online)
	{
		sgm->psy_usb_type = POWER_SUPPLY_USB_TYPE_UNKNOWN;
		sgm->chg_type = POWER_SUPPLY_TYPE_UNKNOWN;
		sgm->float_count = 0;
		sgm->hvdcp_detect_count = 0;
		dev_err(sgm->dev, "Vbus not online\n");
		goto err;
	}

	if(!sgm4154x_dpdm_detect_is_done(sgm) && sgm->state.chrg_type == SGM4154x_NOT_CHRGING) {
		dev_err(sgm->dev, "DPDM detecte not done, disable charge\n");
		goto err;
	}
#if (defined(__SGM41542_CHIP_ID__)|| defined(__SGM41516D_CHIP_ID__)|| defined(__SGM41543D_CHIP_ID__))
	switch(sgm->state.chrg_type) {
		case SGM4154x_NOT_CHRGING:
			pr_err("SGM4154x charger type: NOT_CHRGING\n");
			sgm->psy_usb_type = POWER_SUPPLY_USB_TYPE_UNKNOWN;
			sgm->chg_type = POWER_SUPPLY_TYPE_UNKNOWN;
			break;

		case SGM4154x_USB_SDP:
			pr_err("SGM4154x charger type: SDP\n");
			sgm->psy_usb_type = POWER_SUPPLY_USB_TYPE_SDP;
			sgm->chg_type = POWER_SUPPLY_TYPE_USB;
			/*if (sgm->force_detect_count < 3 || (sgm->float_count > 0
				&& sgm->float_to_sdp_count < 3)) {
				sgm->float_to_sdp_count++;
				schedule_delayed_work(&sgm->force_detect_dwork, HZ);
			}*/
			Charger_Detect_Release();
			break;

		case SGM4154x_USB_CDP:
			pr_err("SGM4154x charger type: CDP\n");
			sgm->psy_usb_type = POWER_SUPPLY_USB_TYPE_CDP;
			sgm->chg_type = POWER_SUPPLY_TYPE_USB_CDP;
			/*if (sgm->force_detect_count < 3 || (sgm->float_count > 0
				&& sgm->float_to_sdp_count < 3)) {
				sgm->float_to_sdp_count++;
				schedule_delayed_work(&sgm->force_detect_dwork, HZ);
			}*/
			Charger_Detect_Release();
			break;

		case SGM4154x_USB_DCP:
			pr_err("SGM4154x charger type: DCP\n");
			sgm->psy_usb_type = POWER_SUPPLY_USB_TYPE_DCP;
			sgm->chg_type = POWER_SUPPLY_TYPE_USB_DCP;
			if(sgm->pd_adapter != POWER_SUPPLY_USB_TYPE_PD &&
					sgm->is_hvdcp_type != true)
				schedule_delayed_work(&sgm->hvdcp_work, 0);
			break;

		case SGM4154x_UNKNOWN:
			pr_err("SGM4154x charger type: FLOAT, count is %d\n", sgm->float_count);
			if(sgm->float_count > 1) {
				sgm->float_count = 2;
				sgm->psy_usb_type = POWER_SUPPLY_USB_TYPE_APPLE_BRICK_ID;
			} else {
				sgm->psy_usb_type = POWER_SUPPLY_USB_TYPE_UNKNOWN;
			}
			sgm->chg_type = POWER_SUPPLY_TYPE_USB;
			if (sgm->force_detect_count < 300 &&
				sgm->pd_adapter != POWER_SUPPLY_USB_TYPE_PD) {
				schedule_delayed_work(&sgm->force_detect_dwork, HZ);
				sgm->non_std_rerun = true;
			}
			sgm->float_count ++;
			break;

		case SGM4154x_NON_STANDARD:
			pr_err("SGM4154x charger type: NON_STAND\n");
			if (sgm->non_std_rerun) {
				schedule_delayed_work(&sgm->force_detect_dwork, HZ);
				sgm->non_std_rerun = false;
			} else {
				sgm->psy_usb_type = POWER_SUPPLY_USB_TYPE_DCP;
				sgm->chg_type = POWER_SUPPLY_TYPE_USB_DCP;
				sgm->adp_type = get_nonstand_charge_type(sgm);
				sgm->is_nonstand_adapter = true;
			}
			break;

		default:
			pr_err("SGM4154x charger type: default\n");
			sgm->psy_usb_type = POWER_SUPPLY_USB_TYPE_UNKNOWN;
			sgm->chg_type = POWER_SUPPLY_TYPE_UNKNOWN;
			break;
	}


#endif
	sgm4154x_enable_charger(sgm);
	if(sgm->pd_adapter == POWER_SUPPLY_USB_TYPE_PD) {
		sgm4154x_get_state(sgm, &state);
		mutex_lock(&sgm->lock);
		sgm->state = state;
		mutex_unlock(&sgm->lock);
	}
	sgm4154x_dump_register(sgm->chg_dev);

err:
	//release wakelock
	power_supply_changed(sgm->charger);
	dev_err(sgm->dev, "Relax wakelock\n");

	return IRQ_HANDLED;
}
static char *sgm4154x_charger_supplied_to[] = {
    "battery",
    "mtk-master-charger",
};

static struct power_supply_desc sgm4154x_power_supply_desc = {
	.name = "sgm4154x-charger",
	.type = POWER_SUPPLY_TYPE_UNKNOWN,
	.usb_types = sgm4154x_usb_type,
	.num_usb_types = ARRAY_SIZE(sgm4154x_usb_type),
	.properties = sgm4154x_power_supply_props,
	.num_properties = ARRAY_SIZE(sgm4154x_power_supply_props),
	.get_property = sgm4154x_charger_get_property,
	.set_property = sgm4154x_charger_set_property,
	.property_is_writeable = sgm4154x_property_is_writeable,
};

static int sgm4154x_power_supply_init(struct sgm4154x_device *sgm,
							struct device *dev)
{
	struct power_supply_config psy_cfg = { .drv_data = sgm,
						.of_node = dev->of_node, };

	psy_cfg.supplied_to = sgm4154x_charger_supplied_to;
	psy_cfg.num_supplicants = ARRAY_SIZE(sgm4154x_charger_supplied_to);

	sgm->charger = devm_power_supply_register(sgm->dev,
						 &sgm4154x_power_supply_desc,
						 &psy_cfg);
	if (IS_ERR(sgm->charger))
		return -EINVAL;
	
	return 0;
}

static int sgm4154x_hw_init(struct sgm4154x_device *sgm)
{
	int ret = 0;	
	struct power_supply_battery_info bat_info = { };	

	bat_info.constant_charge_current_max_ua =
			SGM4154x_ICHRG_I_DEF_uA;

	bat_info.constant_charge_voltage_max_uv =
			SGM4154x_VREG_V_DEF_uV;

	bat_info.precharge_current_ua =
			SGM4154x_PRECHRG_I_DEF_uA;

	bat_info.charge_term_current_ua =
			SGM4154x_TERMCHRG_I_DEF_uA;

	sgm->init_data.max_ichg =
			SGM4154x_ICHRG_I_MAX_uA;

	sgm->init_data.max_vreg =
			SGM4154x_VREG_V_MAX_uV;
			
	sgm4154x_set_watchdog_timer(sgm,0);

	ret = sgm4154x_set_ichrg_curr(s_chg_dev_otg,
				bat_info.constant_charge_current_max_ua);
	if (ret)
		goto err_out;

	ret = sgm4154x_set_prechrg_curr(sgm, bat_info.precharge_current_ua);
	if (ret)
		goto err_out;

	ret = sgm4154x_set_chrg_volt(s_chg_dev_otg,
				bat_info.constant_charge_voltage_max_uv);
	if (ret)
		goto err_out;

	ret = sgm4154x_set_term_curr(s_chg_dev_otg, bat_info.charge_term_current_ua);
	if (ret)
		goto err_out;

	/*ret = sgm4154x_set_input_volt_lim(sgm, sgm->init_data.vlim);
	if (ret)
		goto err_out;*/

	ret = sgm4154x_set_input_curr_lim(s_chg_dev_otg, sgm->init_data.ilim);
	if (ret)
		goto err_out;
	#if 0
	ret = sgm4154x_set_vac_ovp(sgm);//14V
	if (ret)
		goto err_out;	
	#endif
	ret = sgm4154x_set_recharge_volt(sgm, 100);//100~200mv
	if (ret)
		goto err_out;
	
	dev_notice(sgm->dev, "ichrg_curr:%d prechrg_curr:%d chrg_vol:%d"
		" term_curr:%d input_curr_lim:%d",
		bat_info.constant_charge_current_max_ua,
		bat_info.precharge_current_ua,
		bat_info.constant_charge_voltage_max_uv,
		bat_info.charge_term_current_ua,
		sgm->init_data.ilim);

	return 0;

err_out:
	return ret;

}

static int sgm4154x_parse_dt(struct sgm4154x_device *sgm)
{
	int ret;	
	int irq_gpio = 0, irqn = 0;
	//int chg_en_gpio = 0;	
	
	ret = device_property_read_u32(sgm->dev,
				       "input-voltage-limit-microvolt",
				       &sgm->init_data.vlim);
	if (ret)
		sgm->init_data.vlim = SGM4154x_VINDPM_DEF_uV;

	if (sgm->init_data.vlim > SGM4154x_VINDPM_V_MAX_uV ||
	    sgm->init_data.vlim < SGM4154x_VINDPM_V_MIN_uV)
		return -EINVAL;

	ret = device_property_read_u32(sgm->dev,
				       "input-current-limit-microamp",
				       &sgm->init_data.ilim);
	if (ret)
		sgm->init_data.ilim = SGM4154x_IINDPM_DEF_uA;

	if (sgm->init_data.ilim > SGM4154x_IINDPM_I_MAX_uA ||
	    sgm->init_data.ilim < SGM4154x_IINDPM_I_MIN_uA)
		return -EINVAL;

	irq_gpio = of_get_named_gpio(sgm->dev->of_node, "sgm,irq-gpio", 0);
	if (!gpio_is_valid(irq_gpio))
	{
		dev_err(sgm->dev, "%s: %d gpio get failed\n", __func__, irq_gpio);
		return -EINVAL;
	}
	ret = gpio_request(irq_gpio, "sgm4154x irq pin");
	if (ret) {
		dev_err(sgm->dev, "%s: %d gpio request failed\n", __func__, irq_gpio);
		return ret;
	}
	gpio_direction_input(irq_gpio);
	irqn = gpio_to_irq(irq_gpio);
	if (irqn < 0) {
		dev_err(sgm->dev, "%s:%d gpio_to_irq failed\n", __func__, irqn);
		return irqn;
	}
	sgm->client->irq = irqn;
#if 0 	//longcheer wangbin remove it,hardware pulldown to ground.
	chg_en_gpio = of_get_named_gpio(sgm->dev->of_node, "sgm,chg-en-gpio", 0);
	if (!gpio_is_valid(chg_en_gpio))
	{
		dev_err(sgm->dev, "%s: %d gpio get failed\n", __func__, chg_en_gpio);
		return -EINVAL;
	}
	ret = gpio_request(chg_en_gpio, "sgm chg en pin");
	if (ret) {
		dev_err(sgm->dev, "%s: %d gpio request failed\n", __func__, chg_en_gpio);
		return ret;
	}
	gpio_direction_output(chg_en_gpio,0);//default enable charge
#endif
	sgm->bc12_down = false;
	sgm->is_suspend = false;
	sgm->is_hvdcp_type = false;
	return 0;
}

static int sgm4154x_enable_vbus(struct regulator_dev *rdev)
{	
	int ret = 0;
	struct sgm4154x_device *sgm = charger_get_data(s_chg_dev_otg);
	if(IS_ERR_OR_NULL(sgm))
		return -ENOMEM;
	ret = sgm4154x_update_bits(sgm, SGM4154x_CHRG_CTRL_1, SGM4154x_OTG_EN,
                     SGM4154x_OTG_EN);
	return ret;
}

static int sgm4154x_disable_vbus(struct regulator_dev *rdev)
{
	int ret = 0;
	struct sgm4154x_device *sgm = charger_get_data(s_chg_dev_otg);
	if(IS_ERR_OR_NULL(sgm))
		return -ENOMEM;
	ret = sgm4154x_update_bits(sgm, SGM4154x_CHRG_CTRL_1, SGM4154x_OTG_EN,
                     0);

	return ret;
}

#if 0
static int sgm4154x_is_enabled_vbus(struct regulator_dev *rdev)
{
	u8 temp = 0;
	int ret = 0;
	struct sgm4154x_device *sgm = charger_get_data(s_chg_dev_otg);	

	ret = sgm4154x_read_reg(sgm, SGM4154x_CHRG_CTRL_1, &temp);
	return (temp&SGM4154x_OTG_EN)? 1 : 0;
}
#endif

static int sgm4154x_do_event(struct charger_device *chg_dev, u32 event, u32 args) {
	struct sgm4154x_device *sgm = dev_get_drvdata(&chg_dev->dev);
	struct sgm4154x_state state = { };
	int ret = 0;
	if(IS_ERR_OR_NULL(sgm))
		return -ENOMEM;
	dev_info(sgm->dev, "%s, event =%d\n", __func__, event);

	ret = sgm4154x_get_state(sgm, &state);
	mutex_lock(&sgm->lock);
	sgm->state = state;
	mutex_unlock(&sgm->lock);

	switch (event) {
	case EVENT_FULL:
		sgm->chg_done = true;
		power_supply_changed(sgm->charger);
		chg_wakelock(sgm, false);
		break;
	case EVENT_RECHARGE:
		sgm->chg_done = false;
		power_supply_changed(sgm->charger);
		break;
	case EVENT_DISCHARGE:
		sgm->chg_done = false;
		power_supply_changed(sgm->charger);
		break;
	default:
		break;
	}

	return ret;
}

static int sgm4154x_enable_otg(struct charger_device *chg_dev, bool en)
{
	int ret = 0;

	pr_info("%s en = %d\n", __func__, en);
	if (en) {
		ret = sgm4154x_enable_vbus(NULL);
	} else {
		ret = sgm4154x_disable_vbus(NULL);
	}
	return ret;
}

#if(0)
static int sgm4154x_get_is_power_path_enable(struct charger_device *chg_dev, bool *en){
    struct sgm4154x_device *sgm = charger_get_data(chg_dev);
	u32 vlim;
	int ret = 0;

	if (IS_ERR_OR_NULL(sgm))
		return -ENOMEM;

	ret = sgm4154x_get_input_volt_lim(sgm, &vlim);
	if (ret < 0)
		return ret;

	*en = (vlim == VINDPM_MAX) ? false : true;


	return 0;
}

static int sgm4154x_enable_power_path(struct charger_device *chg_dev, bool en)
{
	struct sgm4154x_device *sgm = charger_get_data(chg_dev);
	u32 mivr;

	if (IS_ERR_OR_NULL(sgm))
		return -ENOMEM;

	mivr = (en ? VINDPM_DEFAULT : VINDPM_MAX);
	sgm4154x_set_input_volt_lim(sgm->chg_dev,mivr);

    return 0;
}
#endif

static int sgm4154x_set_boost_voltage_limit(struct charger_device
		*chg_dev, u32 uV)
{	
	int ret = 0;
	char reg_val = -1;
	int i = 0;
	struct sgm4154x_device *sgm = charger_get_data(chg_dev);
	
	while(i<4){
		if (uV == BOOST_VOLT_LIMIT[i]){
			reg_val = i;
			break;
		}
		i++;
	}
	if (reg_val < 0)
		return reg_val;
	reg_val = reg_val << 4;
	ret = sgm4154x_update_bits(sgm, SGM4154x_CHRG_CTRL_6,
				  SGM4154x_BOOSTV, reg_val);

	return ret;
}

static int sgm4154x_set_boost_current_limit(struct charger_device *chg_dev, u32 uA)
{	
	int ret = 0;
	struct sgm4154x_device *sgm = charger_get_data(chg_dev);
	if(IS_ERR_OR_NULL(sgm))
		return -ENOMEM;
	if (uA == BOOST_CURRENT_LIMIT[0]){
		ret = sgm4154x_update_bits(sgm, SGM4154x_CHRG_CTRL_2, SGM4154x_BOOST_LIM,
                     0); 
	}
		
	else if (uA == BOOST_CURRENT_LIMIT[1]){
		ret = sgm4154x_update_bits(sgm, SGM4154x_CHRG_CTRL_2, SGM4154x_BOOST_LIM,
                     BIT(7)); 
	}
	return ret;
}

#if 0
static struct regulator_ops sgm4154x_vbus_ops = {
	.enable = sgm4154x_enable_vbus,
	.disable = sgm4154x_disable_vbus,
	.is_enabled = sgm4154x_is_enabled_vbus,
};

static const struct regulator_desc sgm4154x_otg_rdesc = {
	.of_match = "usb-otg-vbus",
	.name = "usb-otg-vbus",
	.ops = &sgm4154x_vbus_ops,
	.owner = THIS_MODULE,
	.type = REGULATOR_VOLTAGE,
	.fixed_uV = 5000000,
	.n_voltages = 1,
};

static int sgm4154x_vbus_regulator_register(struct sgm4154x_device *sgm)
{
	struct regulator_config config = {};
	int ret = 0;
	/* otg regulator */
	config.dev = sgm->dev;
	config.driver_data = sgm;
	sgm->otg_rdev = devm_regulator_register(sgm->dev,
						&sgm4154x_otg_rdesc, &config);
	sgm->otg_rdev->constraints->valid_ops_mask |= REGULATOR_CHANGE_STATUS;
	if (IS_ERR(sgm->otg_rdev)) {
		ret = PTR_ERR(sgm->otg_rdev);
		pr_info("%s: register otg regulator failed (%d)\n", __func__, ret);
	}
	return ret;
}
#endif

static int sgm4154x_pd_adapter(struct charger_device *chg_dev, int *pd_adapter) {
	struct sgm4154x_device *sgm = dev_get_drvdata(&chg_dev->dev);

	dev_info(sgm->dev, "%s pd_adapter = %d\n", __func__, *pd_adapter);
	sgm->pd_adapter = *pd_adapter;

	return 0;
}

static struct charger_ops sgm4154x_chg_ops = {
	.enable_hz = sgm4154x_set_hiz_en,
	//.is_enable_hz =sgm4154x_is_enable_hiz,
		
	.dump_registers = sgm4154x_dump_register,
	/* cable plug in/out */
   //.plug_in = mt6375_plug_in,
   //.plug_out = mt6375_plug_out,
   /* enable */
   .enable = sgm4154x_charging_switch,
   .is_enabled = sgm4154x_is_charging,
   /* charging current */
   .set_charging_current = sgm4154x_set_ichrg_curr,
   .get_charging_current = sgm4154x_get_ichg_curr,
   //.get_min_charging_current = sgm4154x_get_minichg_curr,
   /* charging voltage */
   .set_constant_voltage = sgm4154x_set_chrg_volt,
   .get_constant_voltage = sgm4154x_get_chrg_volt,
   /* input current limit */
   .set_input_current = sgm4154x_set_input_curr_lim,
   .get_input_current = sgm4154x_get_input_curr_lim,
   .get_min_input_current = sgm4154x_get_input_mincurr_lim,
   /* MIVR */
   .set_mivr = sgm4154x_set_input_volt_lim,
   //.get_mivr = sgm4154x_get_input_volt_lim,
   .get_mivr_state = sgm4154x_get_input_minvolt_lim,
   /* ADC */
   //.get_adc = mt6375_get_adc,
   //.get_vbus_adc = mt6375_get_vbus,
   //.get_ibus_adc = mt6375_get_ibus,
   //.get_ibat_adc = mt6375_get_ibat,
   //.get_tchg_adc = mt6375_get_tchg,
   //.get_zcv = mt6375_get_zcv,
   /* charing termination */
   .set_eoc_current = sgm4154x_set_term_curr,
   //.enable_termination = mt6375_enable_te,
   //.reset_eoc_state = mt6375_reset_eoc_state,
   //.safety_check = mt6375_sw_check_eoc,
   .is_charging_done = sgm4154x_get_charging_status,
   /* power path */
   //.enable_powerpath = mt6375_enable_buck,
   //.is_powerpath_enabled = mt6375_is_buck_enabled,
   /* timer */
   .enable_safety_timer = sgm4154x_enable_safetytimer,
   .is_safety_timer_enabled = sgm4154x_get_is_safetytimer_enable,
	/* Power path */
	//.enable_powerpath = sgm4154x_enable_power_path,
	//.is_powerpath_enabled = sgm4154x_get_is_power_path_enable,
   .kick_wdt = sgm4154x_reset_watch_dog_timer,
   /* AICL */
   //.run_aicl = mt6375_run_aicc,
	.event = sgm4154x_do_event,
   /* PE+/PE+20 */
   #if (defined(__SGM41542_CHIP_ID__)|| defined(__SGM41516D_CHIP_ID__)|| defined(__SGM41543D_CHIP_ID__))
	.send_ta_current_pattern = sgm4154x_en_pe_current_partern,
#else
	.send_ta_current_pattern = NULL,
#endif
   //.set_pe20_efficiency_table = mt6375_set_pe20_efficiency_table,
   //.send_ta20_current_pattern = mt6375_set_pe20_current_pattern,
   //.reset_ta = mt6375_reset_pe_ta,
   //.enable_cable_drop_comp = mt6
   /* OTG */
	.enable_otg = sgm4154x_enable_otg,	
    .set_boost_current_limit = sgm4154x_set_boost_voltage_limit,
	.set_boost_current_limit = sgm4154x_set_boost_current_limit,
	.set_pd_adapter = sgm4154x_pd_adapter,
	.get_property = sgm4154x_get_property,
	.set_vreg_ft = sgm4154x_set_vreg_ft,
};

//sysfs by hjh
static ssize_t hiz_enable_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct sgm4154x_device *sgm = dev_get_drvdata(dev->parent); // 注意：dev 是 power_supply 设备，它的 parent 是 i2c 设备，其 drvdata 指向 sgm
	bool en;
	int ret;

	if (!sgm || !sgm->chg_dev)
		return -EINVAL;

	ret = sgm4154x_is_enable_hiz(sgm->chg_dev, &en);
	if (ret)
		return ret;

	return scnprintf(buf, PAGE_SIZE, "%d\n", en ? 1 : 0);
}

static ssize_t hiz_enable_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct sgm4154x_device *sgm = dev_get_drvdata(dev->parent);
	bool en;
	int ret;

	if (!sgm || !sgm->chg_dev)
		return -EINVAL;

	ret = kstrtobool(buf, &en);
	if (ret)
		return ret;

	ret = sgm4154x_set_hiz_en(sgm->chg_dev, en);
	if (ret)
		return ret;

	return count;
}

static DEVICE_ATTR_RW(hiz_enable);

static ssize_t charging_enable_show(struct device *dev,
                                    struct device_attribute *attr, char *buf)
{
    struct sgm4154x_device *sgm = dev_get_drvdata(dev->parent);
    bool en;
    int ret;

    if (!sgm || !sgm->chg_dev)
        return -EINVAL;

    ret = sgm4154x_is_charging(sgm->chg_dev, &en);
    if (ret)
        return ret;

    return scnprintf(buf, PAGE_SIZE, "%d\n", en ? 1 : 0);
}

static ssize_t charging_enable_store(struct device *dev,
                                      struct device_attribute *attr,
                                      const char *buf, size_t count)
{
    struct sgm4154x_device *sgm = dev_get_drvdata(dev->parent);
    bool en;
    int ret;

    if (!sgm || !sgm->chg_dev)
        return -EINVAL;

    ret = kstrtobool(buf, &en);
    if (ret)
        return ret;

    ret = sgm4154x_charging_switch(sgm->chg_dev, en);
    if (ret)
        return ret;

    return count;
}

static DEVICE_ATTR_RW(charging_enable);


static int sgm4154x_driver_probe(struct i2c_client *client)
{
	int ret = 0;
	struct device *dev = &client->dev;
	struct sgm4154x_device *sgm;

    char *name = NULL;
	
	pr_info("[%s]\n", __func__);

	sgm = devm_kzalloc(dev, sizeof(*sgm), GFP_KERNEL);
	if (!sgm)
		return -ENOMEM;

	sgm->client = client;
	sgm->dev = dev;	
	
	mutex_init(&sgm->lock);
	mutex_init(&sgm->i2c_rw_lock);
	
	i2c_set_clientdata(client, sgm);

	ret = sgm4154x_hw_chipid_detect(sgm);
	/*
	if (ret != SGM4154x_PN_ID){
		pr_info("[%s] device not found !!!\n", __func__);
		return ret;
	}*/

	if (ret != SGM4154x_PN_ID){
		strcpy(Exists_IC_Name, "sc8989xh");
		pr_info("[%s] sgm4154x device not found,  current charging IC is %s!!!\n", __func__, Exists_IC_Name);
		return ret;
	}else if(ret == SGM4154x_PN_ID){
		strcpy(Exists_IC_Name, "sgm4154x");
		pr_info("[%s] sgm4154x device is found,  current charging IC is %s!!!\n", __func__, Exists_IC_Name);
	}

	ret = sgm4154x_parse_dt(sgm);
	if (ret)
		return ret;

	name = devm_kasprintf(sgm->dev, GFP_KERNEL, "%s","sgm4154x suspend wakelock");
	sgm->charger_wakelock =	wakeup_source_register(sgm->dev,name);
	
	/* Register charger device */
	sgm->chg_dev = charger_device_register("primary_chg",
						&client->dev, sgm,
						&sgm4154x_chg_ops,
						&sgm4154x_chg_props);
	if (IS_ERR_OR_NULL(sgm->chg_dev)) {
		pr_info("%s: register charger device  failed\n", __func__);
		ret = PTR_ERR(sgm->chg_dev);
		return ret;
	}    
	
	/* otg regulator */
	s_chg_dev_otg=sgm->chg_dev;

	INIT_DELAYED_WORK(&sgm->charge_monitor_work, charger_monitor_work_func);
	INIT_DELAYED_WORK(&sgm->force_detect_dwork, sgm4154x_force_detection_dwork_handler);
	INIT_DELAYED_WORK(&sgm->hvdcp_work, sgm4154x_chg_type_hvdcp_work);
	INIT_DELAYED_WORK(&sgm->first_sdp_detect_dwork,
                        sgm4151x_first_sdp_detection_dwork_handler);
	schedule_delayed_work(&sgm->first_sdp_detect_dwork,
                        msecs_to_jiffies(1500));
	if (client->irq) {
		ret = devm_request_threaded_irq(dev, client->irq, NULL,
						sgm4154x_irq_handler_thread,
						IRQF_TRIGGER_FALLING |
						IRQF_ONESHOT,
						dev_name(&client->dev), sgm);
		if (ret)
			return ret;
		enable_irq_wake(client->irq);
	}	
	
	ret = sgm4154x_power_supply_init(sgm, dev);
	if (ret) {
		pr_err("Failed to register power supply\n");
		return ret;
	}

    // 创建 hiz_enable 属性
	ret = device_create_file(&sgm->charger->dev, &dev_attr_hiz_enable);
	if (ret)
		dev_warn(dev, "Failed to create hiz_enable sysfs file\n");
    // 创建 charging_enable 属性
    ret = device_create_file(&sgm->charger->dev, &dev_attr_charging_enable);
    if (ret)
        dev_warn(dev, "Failed to create charging_enable sysfs file\n");

	ret = sgm4154x_hw_init(sgm);
	if (ret) {
		dev_err(dev, "Cannot initialize the chip.\n");
		return ret;
	}

	sgm4154x_update_bits(sgm, SGM4154x_CHRG_CTRL_a, SGM4154x_VSYS_STAT,
                     SGM4154x_VSYS_STAT);
	sgm4154x_update_bits(sgm, SGM4154x_CHRG_CTRL_a, SGM4154x_THERM_STAT,
                     SGM4154x_THERM_STAT);
	sgm4154x_update_bits(sgm, SGM4154x_CHRG_CTRL_7, SGM4154X_BATFET_RESET_MASK,
					SGM4154x_BATFET_RESET_OFF);
	sgm->state.chrg_stat = -1;
	sgm4154x_irq_handler_thread(client->irq, sgm);
	schedule_delayed_work(&sgm->charge_monitor_work,100);
	dev_err(dev, "sgm41512 probe successfully!\n");

	return ret;

}
EXPORT_SYMBOL(Exists_IC_Name);

static void sgm4154x_charger_remove(struct i2c_client *client)
{
    struct sgm4154x_device *sgm = i2c_get_clientdata(client);

    device_remove_file(&sgm->charger->dev, &dev_attr_hiz_enable);        // add by andy
    device_remove_file(&sgm->charger->dev, &dev_attr_charging_enable);  //add by andy

    cancel_delayed_work_sync(&sgm->charge_monitor_work);

    regulator_unregister(sgm->otg_rdev);

    power_supply_unregister(sgm->charger); 
	
	mutex_destroy(&sgm->lock);
    mutex_destroy(&sgm->i2c_rw_lock);       

    return;
}

static void sgm4154x_charger_shutdown(struct i2c_client *client)
{
	/*Barley code for OBARLEY-9160 by huyh10 at 20230914 start*/
	int ret = 0;
	struct sgm4154x_device *sgm = i2c_get_clientdata(client);
	int entry_shipmode = 0;

	if(IS_ERR_OR_NULL(sgm))
		return;

	entry_shipmode = get_entry_ship_mode();
	pr_err("entry_shipmode:%d\n",entry_shipmode);
	if(entry_shipmode) {
		sgm4154x_update_bits(sgm, SGM4154x_CHRG_CTRL_7, SGM4154x_BATFET_DIS_MASK,
			SGM4154x_BATFET_OFF);
	}

	ret = sgm4154x_disable_charger(sgm);
	if (ret) {
		pr_err("Failed to disable charger, ret = %d\n", ret);
	}
	pr_info("sgm4154x_charger_shutdown\n");


}

static const struct i2c_device_id sgm4154x_i2c_ids[] = {
	{ "sgm41541", 0 },
	{ "sgm41542", 1 },
	{ "sgm41543", 2 },
	{ "sgm41543D", 3 },
	{ "sgm41513", 4 },
	{ "sgm41513A", 5 },
	{ "sgm41513D", 6 },
	{ "sgm41516", 7 },
	{ "sgm41516D", 8 },
	{},
};
MODULE_DEVICE_TABLE(i2c, sgm4154x_i2c_ids);

static const struct of_device_id sgm4154x_of_match[] = {
	{ .compatible = "sgm,sgm41541", },
	{ .compatible = "sgm,sgm41542", },
	{ .compatible = "sgm,sgm41543", },
	{ .compatible = "sgm,sgm41543D", },
	{ .compatible = "sgm,sgm41513", },
	{ .compatible = "sgm,sgm41513A", },
	{ .compatible = "sgm,sgm41513D", },
	{ .compatible = "sgm,sgm41516", },
	{ .compatible = "sgm,sgm41516D", },
	{ },
};
MODULE_DEVICE_TABLE(of, sgm4154x_of_match);

/*Barley code for OBARLEY-2743 by liyw37 at 20230905 start*/
#ifdef CONFIG_PM_SLEEP
static int sgm4154x_suspend(struct device *dev) {
	struct sgm4154x_device *sgm = dev_get_drvdata(dev);

	if(IS_ERR_OR_NULL(sgm))
		return -ENOMEM;

	sgm->is_suspend = true;
	dev_info(dev, "%s\n", __func__);
	if (device_may_wakeup(dev))
		enable_irq_wake(sgm->client->irq);

	disable_irq(sgm->client->irq);

	return 0;
}

static int sgm4154x_resume(struct device *dev) {
	struct sgm4154x_device *sgm = dev_get_drvdata(dev);

	if(IS_ERR_OR_NULL(sgm))
		return -ENOMEM;

	sgm->is_suspend = false;
	dev_info(dev, "%s\n", __func__);
	enable_irq(sgm->client->irq);
	if (device_may_wakeup(dev))
		disable_irq_wake(sgm->client->irq);

	return 0;
}

static const struct dev_pm_ops sgm4154x_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(sgm4154x_suspend, sgm4154x_resume)
};
#endif	/* CONFIG_PM_SLEEP */
/*Barley code for OBARLEY-2743 by liyw37 at 20230905 end*/
static struct i2c_driver sgm4154x_driver = {
	.driver = {
		.name = "sgm4154x-charger",
		.of_match_table = sgm4154x_of_match,
/*Barley code for OBARLEY-2743 by liyw37 at 20230905 start*/
#ifdef CONFIG_PM_SLEEP
		.pm = &sgm4154x_pm_ops,
#endif
/*Barley code for OBARLEY-6460 by liyw37 at 20230905 end*/
	},
	.probe = sgm4154x_driver_probe,
	.remove = sgm4154x_charger_remove,
	.shutdown = sgm4154x_charger_shutdown,
	.id_table = sgm4154x_i2c_ids,
};
module_i2c_driver(sgm4154x_driver);

MODULE_AUTHOR(" qhq <Allen_qin@sg-micro.com>");
MODULE_DESCRIPTION("sgm4154x charger driver");
MODULE_LICENSE("GPL v2");