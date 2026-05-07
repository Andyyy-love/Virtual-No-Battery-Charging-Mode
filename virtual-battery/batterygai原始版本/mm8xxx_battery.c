// SPDX-License-Identifier: GPL-2.0
/*
 * MM8xxx battery driver
 *
 * Copyright (C) 2023 MITSUMI ELECTRIC CO., LTD. - https://www.mitsumi.co.jp/
 *     Yasuhiro Kinoshita <ykinoshita.a2@minebeamitsumi.com>
 *     Takayuki Sugaya <tsugaya.a2@minebeamitsumi.com>
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/param.h>
#include <linux/jiffies.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/slab.h>
#include <linux/of.h>

#include "mm8xxx_battery.h"

#define MM8XXX_MANUFACTURER	"MITSUMI ELECTRIC"

/* MM8XXX Flags */
#define MM8XXX_FLAG_SOCF	BIT(1)
#define MM8XXX_FLAG_SOC1	BIT(2)
#define MM8XXX_FLAG_UT		BIT(3)
#define MM8XXX_FLAG_OT		BIT(4)
#define MM8XXX_FLAG_FC		BIT(9)
#define MM8XXX_FLAG_OTD		BIT(14)
#define MM8XXX_FLAG_OTC		BIT(15)

/* MM8013C10 */
#define MM8013C10_FLAG_DSG	BIT(0)
#define MM8013C10_FLAG_SOCF	MM8XXX_FLAG_SOCF
#define MM8013C10_FLAG_SOC1	MM8XXX_FLAG_SOC1
#define MM8013C10_FLAG_UT	MM8XXX_FLAG_UT
#define MM8013C10_FLAG_OT	MM8XXX_FLAG_OT
#define MM8013C10_FLAG_ODC	BIT(5)
#define MM8013C10_FLAG_OCC	BIT(6)
#define MM8013C10_FLAG_OCVTAKEN	BIT(7)
#define MM8013C10_FLAG_CHG	BIT(8)
#define MM8013C10_FLAG_FC	MM8XXX_FLAG_FC
#define MM8013C10_FLAG_CHG_INH	BIT(11)
#define MM8013C10_FLAG_BATLOW	BIT(12)
#define MM8013C10_FLAG_BATHI	BIT(13)
#define MM8013C10_FLAG_OTD	MM8XXX_FLAG_OTD
#define MM8013C10_FLAG_OTC	MM8XXX_FLAG_OTC

/* control register params */
#define MM8XXX_SEALED		0x20
#define MM8XXX_RESET		0x41

#define MM8XXX_RS		(20) /* Resistor sense mOhm */
#define MM8XXX_POWER_CONSTANT	(29200) /* 29.2 µV^2 * 1000 */
#define MM8XXX_CURRENT_CONSTANT	(3570) /* 3.57 µV * 1000 */

#define INVALID_REG_ADDR	0xff

/* old define */
#define MM8XXX_SET_CFGUPDATE	0
#define MM8XXX_SOFT_RESET	0
#define MM8XXX_FLAG_CFGUP	0

/*
 * mm8xxx_reg_index - Register names
 *
 * These are indexes into a device's register mapping array.
 */

enum mm8xxx_reg_index {
	MM8XXX_REG_CTRL = 0,		/* Control */
	MM8XXX_REG_TEMP,		/* Temperature */
	MM8XXX_REG_INT_TEMP,		/* Internal Temperature */
	MM8XXX_REG_VOLT,		/* Voltage */
	MM8XXX_REG_AI,			/* Average Current */
	MM8XXX_REG_FLAGS,		/* Flags */
	MM8XXX_REG_TTE,			/* Time-to-Empty */
	MM8XXX_REG_TTF,			/* Time-to-Full */
	MM8XXX_REG_TTES,		/* Time-to-Empty Standby */
	MM8XXX_REG_TTECP,		/* Time-to-Empty at Constant Power */
	MM8XXX_REG_NAC,			/* Nominal Available Capacity */
	MM8XXX_REG_RC,			/* Remaining Capacity */
	MM8XXX_REG_FCC,			/* Full Charge Capacity */
	MM8XXX_REG_CYCT,		/* Cycle Count */
	MM8XXX_REG_AE,			/* Available Energy */
	MM8XXX_REG_SOC,			/* State-of-Charge */
	MM8XXX_REG_SOH,
	MM8XXX_REG_CHARGEVOLTAGE,	/* Charge Voltage */
	MM8XXX_REG_DCAP,		/* Design Capacity */
	MM8XXX_REG_AP,			/* Average Power */
	MM8XXX_DM_CTRL,			/* Block Data Control */
	MM8XXX_DM_CLASS,		/* Data Class */
	MM8XXX_DM_BLOCK,		/* Data Block */
	MM8XXX_DM_DATA,			/* Block Data */
	MM8XXX_DM_CKSUM,		/* Block Data Checksum */
	MM8XXX_REG_ELAPSEDTIMEM,	/* Elapsed Time "Month" */
	MM8XXX_REG_ELAPSEDTIMED,	/* Elapsed Time "Day" */
	MM8XXX_REG_ELAPSEDTIMEH,	/* Elapsed Time "Hour" */
	MM8XXX_REG_MAX,			/* sentinel */
};

#define MM8XXX_DM_REG_ROWS \
	[MM8XXX_DM_CTRL] = 0x61,  \
	[MM8XXX_DM_CLASS] = 0x3e, \
	[MM8XXX_DM_BLOCK] = 0x3f, \
	[MM8XXX_DM_DATA] = 0x40,  \
	[MM8XXX_DM_CKSUM] = 0x60

/* Register mappings */
static u8
	mm8013c10_regs[MM8XXX_REG_MAX] = {
		[MM8XXX_REG_CTRL] = 0x00,
		[MM8XXX_REG_TEMP] = 0x06,
		[MM8XXX_REG_INT_TEMP] = 0x28,
		[MM8XXX_REG_VOLT] = 0x08,
		[MM8XXX_REG_AI] = 0x14,
		[MM8XXX_REG_FLAGS] = 0x0a,
		[MM8XXX_REG_TTE] = 0x16,
		[MM8XXX_REG_TTF] = INVALID_REG_ADDR,
		[MM8XXX_REG_TTES] = INVALID_REG_ADDR,
		[MM8XXX_REG_TTECP] = INVALID_REG_ADDR,
		[MM8XXX_REG_NAC] = 0x0c,
		[MM8XXX_REG_RC] = 0x10,
		[MM8XXX_REG_FCC] = 0x12,
		[MM8XXX_REG_CYCT] = 0x2a,
		[MM8XXX_REG_AE] = INVALID_REG_ADDR,
		[MM8XXX_REG_SOC] = 0x2c,
		[MM8XXX_REG_SOH] = 0x2E,
		[MM8XXX_REG_CHARGEVOLTAGE] = 0x30,
		[MM8XXX_REG_DCAP] = 0x3c,
		[MM8XXX_REG_AP] = INVALID_REG_ADDR,
		[MM8XXX_REG_ELAPSEDTIMEM] = 0x74,
		[MM8XXX_REG_ELAPSEDTIMED] = 0x76,
		[MM8XXX_REG_ELAPSEDTIMEH] = 0x78,
		MM8XXX_DM_REG_ROWS,
	};

static enum power_supply_property mm8013c10_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_CAPACITY_LEVEL,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_TIME_TO_EMPTY_NOW,
	POWER_SUPPLY_PROP_TIME_TO_EMPTY_AVG,
	POWER_SUPPLY_PROP_TIME_TO_FULL_NOW,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_CHARGE_NOW,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	POWER_SUPPLY_PROP_ENERGY_FULL_DESIGN,
	POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN,
	POWER_SUPPLY_PROP_CYCLE_COUNT,
	POWER_SUPPLY_PROP_ENERGY_NOW,
	POWER_SUPPLY_PROP_POWER_AVG,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_MANUFACTURER,
};

struct mm8xxx_dm_reg {
	u8 subclass_id;
	u8 offset;
	u8 bytes;
	u16 min, max;
};

enum mm8xxx_dm_reg_id {
	MM8XXX_DM_DESIGN_CAPACITY = 0,
	MM8XXX_DM_DESIGN_ENERGY,
	MM8XXX_DM_TERMINATE_VOLTAGE,
};

#define mm8013c10_dm_regs 0

#define MM8XXX_O_ZERO		BIT(0)
#define MM8XXX_O_OTDC		BIT(1) /* has OTC/OTD overtemperature flags */
#define MM8XXX_O_UTOT		BIT(2) /* has OT overtemperature flag */
#define MM8XXX_O_CFGUP		BIT(3)
#define MM8XXX_O_RAM		BIT(4)
#define MM8013C10_O_BITS	BIT(5)
#define MM8XXX_O_SOC_SI		BIT(6) /* SoC is single register */
#define MM8XXX_O_HAS_CI		BIT(7) /* has Capacity Inaccurate flag */
#define MM8XXX_O_MUL_CHEM	BIT(8) /* multiple chemistries supported */

#define MM8XXX_DATA(ref, key, opt) {		\
	.opts = (opt),				\
	.unseal_key = key,			\
	.regs  = ref##_regs,			\
	.dm_regs = ref##_dm_regs,		\
	.props = ref##_props,			\
	.props_size = ARRAY_SIZE(ref##_props) }

static struct {
	u32 opts;
	u32 unseal_key;
	u8 *regs;
	struct mm8xxx_dm_reg *dm_regs;
	enum power_supply_property *props;
	size_t props_size;
} mm8xxx_chip_data[] = {
	[MM8013C10] = MM8XXX_DATA(mm8013c10,	0,	MM8013C10_O_BITS | MM8XXX_O_OTDC | MM8XXX_O_UTOT),
};

static DEFINE_MUTEX(mm8xxx_list_lock);
static LIST_HEAD(mm8xxx_battery_devices);

#define MM8XXX_MSLEEP(i) usleep_range((i)*1000, (i)*1000+500)

#define MM8XXX_DM_SZ	32

/**
 * struct mm8xxx_dm_buf - chip data memory buffer
 * @class: data memory subclass_id
 * @block: data memory block number
 * @data: data from/for the block
 * @has_data: true if data has been filled by read
 * @dirty: true if data has changed since last read/write
 *
 * Encapsulates info required to manage chip data memory blocks.
 */
struct mm8xxx_dm_buf {
	u8 class;
	u8 block;
	u8 data[MM8XXX_DM_SZ];
	bool has_data, dirty;
};

#define MM8XXX_DM_BUF(di, i) { \
	.class = (di)->dm_regs[i].subclass_id, \
	.block = (di)->dm_regs[i].offset / MM8XXX_DM_SZ, \
}

static inline u16 *mm8xxx_dm_reg_ptr(struct mm8xxx_dm_buf *buf,
				      struct mm8xxx_dm_reg *reg)
{
	if (buf->class == reg->subclass_id &&
	    buf->block == reg->offset / MM8XXX_DM_SZ)
		return (u16 *) (buf->data + reg->offset % MM8XXX_DM_SZ);

	return NULL;
}

static const char * const mm8xxx_dm_reg_name[] = {
	[MM8XXX_DM_DESIGN_CAPACITY] = "design-capacity",
	[MM8XXX_DM_DESIGN_ENERGY] = "design-energy",
	[MM8XXX_DM_TERMINATE_VOLTAGE] = "terminate-voltage",
};






static bool mm8xxx_dt_to_nvm = true;
module_param_named(dt_monitored_battery_updates_nvm, mm8xxx_dt_to_nvm, bool, 0444);
MODULE_PARM_DESC(dt_monitored_battery_updates_nvm,
	"Devicetree monitored-battery config updates data memory on NVM/flash chips.\n"
	"Users must set this =0 when installing a different type of battery!\n"
	"Default is =1."
#ifndef CONFIG_BATTERY_MM8XXX_DT_UPDATES_NVM
	"\nSetting this affects future kernel updates, not the current configuration."
#endif
);

static int poll_interval_param_set(const char *val, const struct kernel_param *kp)
{
	struct mm8xxx_device_info *di;
	unsigned int prev_val = *(unsigned int *) kp->arg;
	int ret;

	ret = param_set_uint(val, kp);
	if (ret < 0 || prev_val == *(unsigned int *) kp->arg)
		return ret;

	mutex_lock(&mm8xxx_list_lock);
	list_for_each_entry(di, &mm8xxx_battery_devices, list) {
		cancel_delayed_work_sync(&di->work);
		schedule_delayed_work(&di->work, 0);
	}
	mutex_unlock(&mm8xxx_list_lock);

	return ret;
}

static const struct kernel_param_ops param_ops_poll_interval = {
	.get = param_get_uint,
	.set = poll_interval_param_set,
};

static unsigned int poll_interval = 360;
module_param_cb(poll_interval, &param_ops_poll_interval, &poll_interval, 0644);
MODULE_PARM_DESC(poll_interval,
		 "battery poll interval in seconds - 0 disables polling");

/*
 * Common code for MM8xxx devices
 */

static inline int mm8xxx_read(struct mm8xxx_device_info *di, int reg_index,
			       bool single)
{
	int ret;

	if (!di || di->regs[reg_index] == INVALID_REG_ADDR)
		return -EINVAL;

	ret = di->bus.read(di, di->regs[reg_index], single);
	if (ret < 0)
		dev_dbg(di->dev, "failed to read register 0x%02x (index %d)\n",
			di->regs[reg_index], reg_index);

	return ret;
}

static inline int mm8xxx_write(struct mm8xxx_device_info *di, int reg_index,
				u16 value, bool single)
{
	int ret;

	if (!di || di->regs[reg_index] == INVALID_REG_ADDR)
		return -EINVAL;

	if (!di->bus.write)
		return -EPERM;

	ret = di->bus.write(di, di->regs[reg_index], value, single);
	if (ret < 0)
		dev_dbg(di->dev, "failed to write register 0x%02x (index %d)\n",
			di->regs[reg_index], reg_index);

	return ret;
}

static inline int mm8xxx_read_block(struct mm8xxx_device_info *di, int reg_index,
				     u8 *data, int len)
{
	int ret;

	if (!di || di->regs[reg_index] == INVALID_REG_ADDR)
		return -EINVAL;

	if (!di->bus.read_bulk)
		return -EPERM;

	ret = di->bus.read_bulk(di, di->regs[reg_index], data, len);
	if (ret < 0)
		dev_dbg(di->dev, "failed to read_bulk register 0x%02x (index %d)\n",
			di->regs[reg_index], reg_index);

	return ret;
}

static inline int mm8xxx_write_block(struct mm8xxx_device_info *di, int reg_index,
				      u8 *data, int len)
{
	int ret;

	if (!di || di->regs[reg_index] == INVALID_REG_ADDR)
		return -EINVAL;

	if (!di->bus.write_bulk)
		return -EPERM;

	ret = di->bus.write_bulk(di, di->regs[reg_index], data, len);
	if (ret < 0)
		dev_dbg(di->dev, "failed to write_bulk register 0x%02x (index %d)\n",
			di->regs[reg_index], reg_index);

	return ret;
}

static int mm8xxx_battery_seal(struct mm8xxx_device_info *di)
{
	int ret;

	ret = mm8xxx_write(di, MM8XXX_REG_CTRL, MM8XXX_SEALED, false);
	if (ret < 0) {
		dev_err(di->dev, "bus error on seal: %d\n", ret);
		return ret;
	}

	return 0;
}

static int mm8xxx_battery_unseal(struct mm8xxx_device_info *di)
{
	int ret;

	if (di->unseal_key == 0) {
		dev_err(di->dev, "unseal failed due to missing key\n");
		return -EINVAL;
	}

	ret = mm8xxx_write(di, MM8XXX_REG_CTRL, (u16)(di->unseal_key >> 16), false);
	if (ret < 0)
		goto out;

	ret = mm8xxx_write(di, MM8XXX_REG_CTRL, (u16)di->unseal_key, false);
	if (ret < 0)
		goto out;

	return 0;

out:
	dev_err(di->dev, "bus error on unseal: %d\n", ret);
	return ret;
}

static u8 mm8xxx_battery_checksum_dm_block(struct mm8xxx_dm_buf *buf)
{
	u16 sum = 0;
	int i;

	for (i = 0; i < MM8XXX_DM_SZ; i++)
		sum += buf->data[i];
	sum &= 0xff;

	return 0xff - sum;
}

static int mm8xxx_battery_read_dm_block(struct mm8xxx_device_info *di,
					 struct mm8xxx_dm_buf *buf)
{
	int ret;

	buf->has_data = false;

	ret = mm8xxx_write(di, MM8XXX_DM_CLASS, buf->class, true);
	if (ret < 0)
		goto out;

	ret = mm8xxx_write(di, MM8XXX_DM_BLOCK, buf->block, true);
	if (ret < 0)
		goto out;

	MM8XXX_MSLEEP(1);

	ret = mm8xxx_read_block(di, MM8XXX_DM_DATA, buf->data, MM8XXX_DM_SZ);
	if (ret < 0)
		goto out;

	ret = mm8xxx_read(di, MM8XXX_DM_CKSUM, true);
	if (ret < 0)
		goto out;

	if ((u8)ret != mm8xxx_battery_checksum_dm_block(buf)) {
		ret = -EINVAL;
		goto out;
	}

	buf->has_data = true;
	buf->dirty = false;

	return 0;

out:
	dev_err(di->dev, "bus error reading chip memory: %d\n", ret);
	return ret;
}

static void mm8xxx_battery_update_dm_block(struct mm8xxx_device_info *di,
					    struct mm8xxx_dm_buf *buf,
					    enum mm8xxx_dm_reg_id reg_id,
					    unsigned int val)
{
	struct mm8xxx_dm_reg *reg = &di->dm_regs[reg_id];
	const char *str = mm8xxx_dm_reg_name[reg_id];
	u16 *prev = mm8xxx_dm_reg_ptr(buf, reg);

	if (prev == NULL) {
		dev_warn(di->dev, "buffer does not match %s dm spec\n", str);
		return;
	}

	if (reg->bytes != 2) {
		dev_warn(di->dev, "%s dm spec has unsupported byte size\n", str);
		return;
	}

	if (!buf->has_data)
		return;

	if (be16_to_cpup(prev) == val) {
		dev_info(di->dev, "%s has %u\n", str, val);
		return;
	}

#if IS_ENABLED(CONFIG_BATTERY_MM8XXX_DT_UPDATES_NVM)
	if (!(di->opts & MM8XXX_O_RAM) && !mm8xxx_dt_to_nvm) {
#else
	if (!(di->opts & MM8XXX_O_RAM)) {
#endif
		/* devicetree and NVM differ; defer to NVM */
		dev_warn(di->dev, "%s has %u; update to %u disallowed "
#if IS_ENABLED(CONFIG_BATTERY_MM8XXX_DT_UPDATES_NVM)
			 "by dt_monitored_battery_updates_nvm=0"
#else
			 "for flash/NVM data memory"
#endif
			 "\n", str, be16_to_cpup(prev), val);
		return;
	}

	dev_info(di->dev, "update %s to %u\n", str, val);

	*prev = cpu_to_be16(val);
	buf->dirty = true;
}

static int mm8xxx_battery_cfgupdate_priv(struct mm8xxx_device_info *di, bool active)
{
	const int limit = 100;
	u16 cmd = active ? MM8XXX_SET_CFGUPDATE : MM8XXX_SOFT_RESET;
	int ret, try = limit;

	ret = mm8xxx_write(di, MM8XXX_REG_CTRL, cmd, false);
	if (ret < 0)
		return ret;

	do {
		MM8XXX_MSLEEP(25);
		ret = mm8xxx_read(di, MM8XXX_REG_FLAGS, false);
		if (ret < 0)
			return ret;
	} while (!!(ret & MM8XXX_FLAG_CFGUP) != active && --try);

	if (limit - try > 3)
		dev_warn(di->dev, "cfgupdate %d, retries %d\n", active, limit - try);

	return 0;
}

static inline int mm8xxx_battery_set_cfgupdate(struct mm8xxx_device_info *di)
{
	int ret = mm8xxx_battery_cfgupdate_priv(di, true);
	if (ret < 0 && ret != -EINVAL)
		dev_err(di->dev, "bus error on set_cfgupdate: %d\n", ret);

	return ret;
}

static inline int mm8xxx_battery_soft_reset(struct mm8xxx_device_info *di)
{
	int ret = mm8xxx_battery_cfgupdate_priv(di, false);

	if (ret < 0 && ret != -EINVAL)
		dev_err(di->dev, "bus error on soft_reset: %d\n", ret);

	return ret;
}

static int mm8xxx_battery_write_dm_block(struct mm8xxx_device_info *di,
					  struct mm8xxx_dm_buf *buf)
{
	bool cfgup = di->opts & MM8XXX_O_CFGUP;
	int ret;

	if (!buf->dirty)
		return 0;

	if (cfgup) {
		ret = mm8xxx_battery_set_cfgupdate(di);
		if (ret < 0)
			return ret;
	}

	ret = mm8xxx_write(di, MM8XXX_DM_CTRL, 0, true);
	if (ret < 0)
		goto out;

	ret = mm8xxx_write(di, MM8XXX_DM_CLASS, buf->class, true);
	if (ret < 0)
		goto out;

	ret = mm8xxx_write(di, MM8XXX_DM_BLOCK, buf->block, true);
	if (ret < 0)
		goto out;

	MM8XXX_MSLEEP(1);

	ret = mm8xxx_write_block(di, MM8XXX_DM_DATA, buf->data, MM8XXX_DM_SZ);
	if (ret < 0)
		goto out;

	ret = mm8xxx_write(di, MM8XXX_DM_CKSUM,
			    mm8xxx_battery_checksum_dm_block(buf), true);
	if (ret < 0)
		goto out;

	/* DO NOT read MM8XXX_DM_CKSUM here to verify it! That may cause NVM
	 * corruption on the '425 chip (and perhaps others), which can damage
	 * the chip.
	 */

	if (cfgup) {
		MM8XXX_MSLEEP(1);
		ret = mm8xxx_battery_soft_reset(di);
		if (ret < 0)
			return ret;
	} else {
		MM8XXX_MSLEEP(100); /* flash DM updates in <100ms */
	}

	buf->dirty = false;

	return 0;

out:
	if (cfgup)
		mm8xxx_battery_soft_reset(di);

	dev_err(di->dev, "bus error writing chip memory: %d\n", ret);
	return ret;
}

static void mm8xxx_battery_set_config(struct mm8xxx_device_info *di,
				       struct power_supply_battery_info *info)
{
	struct mm8xxx_dm_buf bd = MM8XXX_DM_BUF(di, MM8XXX_DM_DESIGN_CAPACITY);
	struct mm8xxx_dm_buf bt = MM8XXX_DM_BUF(di, MM8XXX_DM_TERMINATE_VOLTAGE);
	bool updated;

	if (mm8xxx_battery_unseal(di) < 0)
		return;

	if (info->charge_full_design_uah != -EINVAL &&
	    info->energy_full_design_uwh != -EINVAL) {
		mm8xxx_battery_read_dm_block(di, &bd);
		/* assume design energy & capacity are in same block */
		mm8xxx_battery_update_dm_block(di, &bd,
					MM8XXX_DM_DESIGN_CAPACITY,
					info->charge_full_design_uah / 1000);
		mm8xxx_battery_update_dm_block(di, &bd,
					MM8XXX_DM_DESIGN_ENERGY,
					info->energy_full_design_uwh / 1000);
	}

	if (info->voltage_min_design_uv != -EINVAL) {
		bool same = bd.class == bt.class && bd.block == bt.block;
		if (!same)
			mm8xxx_battery_read_dm_block(di, &bt);
		mm8xxx_battery_update_dm_block(di, same ? &bd : &bt,
					MM8XXX_DM_TERMINATE_VOLTAGE,
					info->voltage_min_design_uv / 1000);
	}

	updated = bd.dirty || bt.dirty;

	mm8xxx_battery_write_dm_block(di, &bd);
	mm8xxx_battery_write_dm_block(di, &bt);

	mm8xxx_battery_seal(di);

	if (updated && !(di->opts & MM8XXX_O_CFGUP)) {
		mm8xxx_write(di, MM8XXX_REG_CTRL, MM8XXX_RESET, false);
		MM8XXX_MSLEEP(300); /* reset time is not documented */
	}
	/* assume mm8xxx_battery_update() is called hereafter */
}

static void mm8xxx_battery_settings(struct mm8xxx_device_info *di)
{
	struct power_supply_battery_info info = {};
	unsigned int min, max;
	struct power_supply_battery_info *sys_info = &info;
	
	dev_err(di->dev, "mm8xxx_battery_settingsd enter\n");
	if (power_supply_get_battery_info(di->bat, &sys_info) < 0)
		return;

	if (!di->dm_regs) {
		dev_warn(di->dev, "data memory update not supported for chip\n");
		return;
	}

	if (info.energy_full_design_uwh != info.charge_full_design_uah) {
		if (info.energy_full_design_uwh == -EINVAL)
			dev_warn(di->dev, "missing battery:energy-full-design-microwatt-hours\n");
		else if (info.charge_full_design_uah == -EINVAL)
			dev_warn(di->dev, "missing battery:charge-full-design-microamp-hours\n");
	}

	/* assume min == 0 */
	max = di->dm_regs[MM8XXX_DM_DESIGN_ENERGY].max;
	if (info.energy_full_design_uwh > max * 1000) {
		dev_err(di->dev, "invalid battery:energy-full-design-microwatt-hours %d\n",
			info.energy_full_design_uwh);
		info.energy_full_design_uwh = -EINVAL;
	}

	/* assume min == 0 */
	max = di->dm_regs[MM8XXX_DM_DESIGN_CAPACITY].max;
	if (info.charge_full_design_uah > max * 1000) {
		dev_err(di->dev, "invalid battery:charge-full-design-microamp-hours %d\n",
			info.charge_full_design_uah);
		info.charge_full_design_uah = -EINVAL;
	}

	min = di->dm_regs[MM8XXX_DM_TERMINATE_VOLTAGE].min;
	max = di->dm_regs[MM8XXX_DM_TERMINATE_VOLTAGE].max;
	if ((info.voltage_min_design_uv < min * 1000 ||
	     info.voltage_min_design_uv > max * 1000) &&
	     info.voltage_min_design_uv != -EINVAL) {
		dev_err(di->dev, "invalid battery:voltage-min-design-microvolt %d\n",
			info.voltage_min_design_uv);
		info.voltage_min_design_uv = -EINVAL;
	}

	if ((info.energy_full_design_uwh != -EINVAL &&
	     info.charge_full_design_uah != -EINVAL) ||
	     info.voltage_min_design_uv  != -EINVAL)
		mm8xxx_battery_set_config(di, &info);
}

/*
 * Return the battery State-of-Charge
 * Or < 0 if something fails.
 */
static int mm8xxx_battery_read_soc(struct mm8xxx_device_info *di)
{
	int soc;

	if (di->opts & MM8XXX_O_SOC_SI)
		soc = mm8xxx_read(di, MM8XXX_REG_SOC, true);
	else
		soc = mm8xxx_read(di, MM8XXX_REG_SOC, false);

	if (soc < 0)
		dev_dbg(di->dev, "error reading State-of-Charge\n");
	
	dev_dbg(di->dev, "mm8xxx_battery_read_soc soc=%d\n", soc);
	return soc;
}

int mm8xxx_battery_read_soh(struct mm8xxx_device_info *di)
{
	int soh;

	soh = mm8xxx_read(di, MM8XXX_REG_SOH, false);

	if (soh < 0)
		dev_dbg(di->dev, "error reading State-of-Charge\n");

	soh = soh + 2;
	if(soh > 100)
		soh = 100;
	
	dev_dbg(di->dev, "mm8xxx_battery_read_soh soh=%d\n", soh);
	return soh;
}
EXPORT_SYMBOL_GPL(mm8xxx_battery_read_soh);
/*
 * Return a battery charge value in µAh
 * Or < 0 if something fails.
 */
static int mm8xxx_battery_read_charge(struct mm8xxx_device_info *di, u8 reg)
{
	int charge;

	dev_dbg(di->dev, "mm8xxx_battery_read_charge enter\n");
	charge = mm8xxx_read(di, reg, false);
	if (charge < 0) {
		dev_dbg(di->dev, "error reading charge register %02x: %d\n",
			reg, charge);
		return charge;
	}

	if (di->opts & MM8XXX_O_ZERO)
		charge *= MM8XXX_CURRENT_CONSTANT / MM8XXX_RS;
	else
		charge *= 1000;

	return charge;
}

/*
 * Return the battery Nominal available capacity in µAh
 * Or < 0 if something fails.
 */
static inline int mm8xxx_battery_read_nac(struct mm8xxx_device_info *di)
{

	if (di->opts & MM8XXX_O_ZERO)
		mm8xxx_read(di, MM8XXX_REG_FLAGS, true);

	return mm8xxx_battery_read_charge(di, MM8XXX_REG_NAC);
}

/*
 * Return the battery Remaining Capacity in µAh
 * Or < 0 if something fails.
 */
static inline int mm8xxx_battery_read_rc(struct mm8xxx_device_info *di)
{
	return mm8xxx_battery_read_charge(di, MM8XXX_REG_RC);
}

/*
 * Return the battery Full Charge Capacity in µAh
 * Or < 0 if something fails.
 */
static inline int mm8xxx_battery_read_fcc(struct mm8xxx_device_info *di)
{
	return mm8xxx_battery_read_charge(di, MM8XXX_REG_FCC);
}

/*
 * Return the Design Capacity in µAh
 * Or < 0 if something fails.
 */
static int mm8xxx_battery_read_dcap(struct mm8xxx_device_info *di)
{
	int dcap;

	if (di->opts & MM8XXX_O_ZERO)
		dcap = mm8xxx_read(di, MM8XXX_REG_DCAP, true);
	else
		dcap = mm8xxx_read(di, MM8XXX_REG_DCAP, false);

	if (dcap < 0) {
		dev_dbg(di->dev, "error reading initial last measured discharge\n");
		return dcap;
	}

	if (di->opts & MM8XXX_O_ZERO)
		dcap = (dcap << 8) * MM8XXX_CURRENT_CONSTANT / MM8XXX_RS;
	else
		dcap *= 1000;

	return dcap;
}

/*
 * Return the battery Available energy in µWh
 * Or < 0 if something fails.
 */
static int mm8xxx_battery_read_energy(struct mm8xxx_device_info *di)
{
	int ae;

	ae = mm8xxx_read(di, MM8XXX_REG_AE, false);
	if (ae < 0) {
		dev_dbg(di->dev, "error reading available energy\n");
		return ae;
	}

	if (di->opts & MM8XXX_O_ZERO)
		ae *= MM8XXX_POWER_CONSTANT / MM8XXX_RS;
	else
		ae *= 1000;

	return ae;
}

/*
 * Return the battery temperature in tenths of degree Kelvin
 * Or < 0 if something fails.
 */
static int mm8xxx_battery_read_temperature(struct mm8xxx_device_info *di)
{
	int temp;

	temp = mm8xxx_read(di, MM8XXX_REG_TEMP, false);
	if (temp < 0) {
		dev_err(di->dev, "error reading temperature\n");
		return temp;
	}

	if (di->opts & MM8XXX_O_ZERO)
		temp = 5 * temp / 2;

	dev_err(di->dev, "mm8xxx_battery_read_temperature  temp=:%d\n", temp);
	return temp;
}

/*
 * Return the battery Cycle count total
 * Or < 0 if something fails.
 */
static int mm8xxx_battery_read_cyct(struct mm8xxx_device_info *di)
{
	int cyct;

	cyct = mm8xxx_read(di, MM8XXX_REG_CYCT, false);
	if (cyct < 0)
		dev_err(di->dev, "error reading cycle count total\n");

	return cyct;
}

/*
 * Read a time register.
 * Return < 0 if something fails.
 */
static int mm8xxx_battery_read_time(struct mm8xxx_device_info *di, u8 reg)
{
	int tval;

	tval = mm8xxx_read(di, reg, false);
	if (tval < 0) {
		dev_dbg(di->dev, "error reading time register %02x: %d\n",
			reg, tval);
		return tval;
	}

	if (tval == 65535)
		return -ENODATA;

	return tval * 60;
}

/*
 * Returns true if a battery over temperature condition is detected
 */
static bool mm8xxx_battery_overtemp(struct mm8xxx_device_info *di, u16 flags)
{
	if ((di->opts & MM8XXX_O_OTDC) && (di->opts & MM8XXX_O_UTOT))
		return flags & (MM8XXX_FLAG_OTC | MM8XXX_FLAG_OTD | MM8XXX_FLAG_OT);
	if (di->opts & MM8XXX_O_OTDC)
		return flags & (MM8XXX_FLAG_OTC | MM8XXX_FLAG_OTD);
	if (di->opts & MM8XXX_O_UTOT)
		return flags & MM8XXX_FLAG_OT;

	return false;
}

/*
 * Returns true if a battery under temperature condition is detected
 */
static bool mm8xxx_battery_undertemp(struct mm8xxx_device_info *di, u16 flags)
{
	if (di->opts & MM8XXX_O_UTOT)
		return flags & MM8XXX_FLAG_UT;

	return false;
}

/*
 * Returns true if a low state of charge condition is detected
 */
static bool mm8xxx_battery_dead(struct mm8xxx_device_info *di, u16 flags)
{
	if (di->opts & MM8013C10_O_BITS)
		return flags & MM8013C10_FLAG_SOCF;
	else
		return flags & (MM8XXX_FLAG_SOC1 | MM8XXX_FLAG_SOCF);
}

static int mm8xxx_battery_read_health(struct mm8xxx_device_info *di)
{
	/* Unlikely but important to return first */
	if (unlikely(mm8xxx_battery_overtemp(di, di->cache.flags)))
		return POWER_SUPPLY_HEALTH_OVERHEAT;
	if (unlikely(mm8xxx_battery_undertemp(di, di->cache.flags)))
		return POWER_SUPPLY_HEALTH_COLD;
	if (unlikely(mm8xxx_battery_dead(di, di->cache.flags)))
		return POWER_SUPPLY_HEALTH_DEAD;

	return POWER_SUPPLY_HEALTH_GOOD;
}



/*
 * Return the battery Charge Voltage
 * Or < 0 if something fails.
 */

int mm8xxx_battery_read_chargevoltage(struct mm8xxx_device_info *di)
{
	int cv;
	cv = mm8xxx_read(di, MM8XXX_REG_CHARGEVOLTAGE, false);
	if (cv < 0)
		dev_err(di->dev, "error reading charge voltage\n");
	return cv;
}
EXPORT_SYMBOL_GPL(mm8xxx_battery_read_chargevoltage);

/*
 * Write the battery Charge Voltage.
 */
int mm8xxx_battery_write_chargevoltage(struct mm8xxx_device_info *di,
		u16 cv)
{
	int ret;
	ret = mm8xxx_write(di, MM8XXX_REG_CHARGEVOLTAGE, cv, false);
	if (ret < 0) {
		dev_err(di->dev, "error writing charge voltage\n");
		return ret;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(mm8xxx_battery_write_chargevoltage);

/*
 * Return the battery usage time in month
 * Or < 0 if something fails.
 */
static int mm8xxx_battery_read_elapsedmonths(struct mm8xxx_device_info *di)
{
	int elapsed;
	elapsed = mm8xxx_read(di, MM8XXX_REG_ELAPSEDTIMEM, false);
	if (elapsed < 0)
		dev_err(di->dev, "error reading elapsed months\n");
	return elapsed;
}
/*
 * Return the battery usage time in day
 * Or < 0 if something fails.
 */
static int mm8xxx_battery_read_elapseddays(struct mm8xxx_device_info *di)
{
	int elapsed;
	elapsed = mm8xxx_read(di, MM8XXX_REG_ELAPSEDTIMED, false);
	if (elapsed < 0)
		dev_err(di->dev, "error reading elapsed days\n");
	return elapsed;
}
/*
 * Return the battery usage time in hour
 * Or < 0 if something fails.
 */
static int mm8xxx_battery_read_elapsedhours(struct mm8xxx_device_info *di)
{
	int elapsed;
	elapsed = mm8xxx_read(di, MM8XXX_REG_ELAPSEDTIMEH, false);
	if (elapsed < 0)
		dev_err(di->dev, "error reading elapsed hours\n");
	return elapsed;
}

void mm8xxx_battery_update(struct mm8xxx_device_info *di)
{
	struct mm8xxx_reg_cache cache = {0, };
	bool has_single_flag = di->opts & MM8XXX_O_ZERO;

	cache.flags = mm8xxx_read(di, MM8XXX_REG_FLAGS, has_single_flag);
	if ((cache.flags & 0xff) == 0xff)
		cache.flags = -1; /* read error */
	if (cache.flags >= 0) {
		cache.temperature = mm8xxx_battery_read_temperature(di);
		if (di->regs[MM8XXX_REG_TTE] != INVALID_REG_ADDR)
			cache.time_to_empty = mm8xxx_battery_read_time(di, MM8XXX_REG_TTE);
		if (di->regs[MM8XXX_REG_TTECP] != INVALID_REG_ADDR)
			cache.time_to_empty_avg = mm8xxx_battery_read_time(di, MM8XXX_REG_TTECP);
		if (di->regs[MM8XXX_REG_TTF] != INVALID_REG_ADDR)
			cache.time_to_full = mm8xxx_battery_read_time(di, MM8XXX_REG_TTF);

		cache.charge_full = mm8xxx_battery_read_fcc(di);
		cache.capacity = mm8xxx_battery_read_soc(di);
		if (di->regs[MM8XXX_REG_AE] != INVALID_REG_ADDR)
			cache.energy = mm8xxx_battery_read_energy(di);
		di->cache.flags = cache.flags;
		cache.health = mm8xxx_battery_read_health(di);
		if (di->regs[MM8XXX_REG_CYCT] != INVALID_REG_ADDR)
			cache.cycle_count = mm8xxx_battery_read_cyct(di);

		/* We only have to read charge design full once */
		if (di->charge_design_full <= 0)
			di->charge_design_full = mm8xxx_battery_read_dcap(di);

		if (di->regs[MM8XXX_REG_ELAPSEDTIMEM] != INVALID_REG_ADDR)
			cache.elapsed_months = mm8xxx_battery_read_elapsedmonths(di);
		if (di->regs[MM8XXX_REG_ELAPSEDTIMED] != INVALID_REG_ADDR)
			cache.elapsed_days = mm8xxx_battery_read_elapseddays(di);
		if (di->regs[MM8XXX_REG_ELAPSEDTIMEH] != INVALID_REG_ADDR)
			cache.elapsed_hours = mm8xxx_battery_read_elapsedhours(di);
	}

	if ((di->cache.capacity != cache.capacity) ||
	    (di->cache.flags != cache.flags) || cache.capacity < 2 || di->cache.capacity < 2)
		power_supply_changed(di->bat);

	if (memcmp(&di->cache, &cache, sizeof(cache)) != 0)
		di->cache = cache;

	di->last_update = jiffies;
	

}
EXPORT_SYMBOL_GPL(mm8xxx_battery_update);

static void mm8xxx_battery_poll(struct work_struct *work)
{
	struct mm8xxx_device_info *di =
			container_of(work, struct mm8xxx_device_info,
				     work.work);

	mm8xxx_battery_update(di);
	if(di->is_pm_suspend(di))
		poll_interval = 360;
	else
		poll_interval = 10;
	if (poll_interval > 0)
		schedule_delayed_work(&di->work, poll_interval * HZ);
}

static bool mm8xxx_battery_is_full(struct mm8xxx_device_info *di, int flags)
{
	if (di->opts & MM8013C10_O_BITS)
		return (flags & MM8013C10_FLAG_FC);
	else
		return (flags & MM8XXX_FLAG_FC);
}

/*
 * Return the battery average current in µA and the status
 * Note that current can be negative signed as well
 * Or 0 if something fails.
 */
static int mm8xxx_battery_current_and_status(
	struct mm8xxx_device_info *di,
	union power_supply_propval *val_curr,
	union power_supply_propval *val_status)
{
	bool single_flags = (di->opts & MM8XXX_O_ZERO);
	int curr;
	int flags;

	curr = mm8xxx_read(di, MM8XXX_REG_AI, false);
	if (curr < 0) {
		dev_err(di->dev, "error reading current\n");
		return curr;
	}

	flags = mm8xxx_read(di, MM8XXX_REG_FLAGS, single_flags);
	if (flags < 0) {
		dev_err(di->dev, "error reading flags\n");
		return flags;
	}

	if (di->opts & MM8XXX_O_ZERO) {
		curr = curr * MM8XXX_CURRENT_CONSTANT / MM8XXX_RS;
	} else {
		/* Other gauges return signed value */
		curr = (int)((s16)curr) * 1000;
	}

	if (val_curr)
		val_curr->intval = curr;

	if (val_status) {
		if (curr > 0) {
			val_status->intval = POWER_SUPPLY_STATUS_CHARGING;
		} else if (curr < 0) {
			val_status->intval = POWER_SUPPLY_STATUS_DISCHARGING;
		} else {
			if (mm8xxx_battery_is_full(di, flags))
				val_status->intval = POWER_SUPPLY_STATUS_FULL;
			else
				val_status->intval =
					POWER_SUPPLY_STATUS_NOT_CHARGING;
		}
	}

	return 0;
}

/*
 * Get the average power in µW
 * Return < 0 if something fails.
 */
static int mm8xxx_battery_pwr_avg(struct mm8xxx_device_info *di,
				   union power_supply_propval *val)
{
	int power;

	power = mm8xxx_read(di, MM8XXX_REG_AP, false);
	if (power < 0) {
		dev_err(di->dev,
			"error reading average power register %02x: %d\n",
			MM8XXX_REG_AP, power);
		return power;
	}

	if (di->opts & MM8XXX_O_ZERO)
		val->intval = (power * MM8XXX_POWER_CONSTANT) / MM8XXX_RS;
	else
		/* Other gauges return a signed value in units of 10mW */
		val->intval = (int)((s16)power) * 10000;

	return 0;
}

static int mm8xxx_battery_capacity_level(struct mm8xxx_device_info *di,
					  union power_supply_propval *val)
{
	int level;

	if (di->opts & MM8XXX_O_ZERO) {
		level = POWER_SUPPLY_CAPACITY_LEVEL_NORMAL;
	} else if (di->opts & MM8013C10_O_BITS) {
		if (di->cache.flags & MM8013C10_FLAG_FC)
			level = POWER_SUPPLY_CAPACITY_LEVEL_FULL;
		else if (di->cache.flags & MM8013C10_FLAG_SOCF)
			level = POWER_SUPPLY_CAPACITY_LEVEL_CRITICAL;
		else
			level = POWER_SUPPLY_CAPACITY_LEVEL_NORMAL;
	} else {
		if (di->cache.flags & MM8XXX_FLAG_FC)
			level = POWER_SUPPLY_CAPACITY_LEVEL_FULL;
		else if (di->cache.flags & MM8XXX_FLAG_SOC1)
			level = POWER_SUPPLY_CAPACITY_LEVEL_LOW;
		else if (di->cache.flags & MM8XXX_FLAG_SOCF)
			level = POWER_SUPPLY_CAPACITY_LEVEL_CRITICAL;
		else
			level = POWER_SUPPLY_CAPACITY_LEVEL_NORMAL;
	}

	val->intval = level;

	return 0;
}

/*
 * Return the battery Voltage in millivolts
 * Or < 0 if something fails.
 */
static int mm8xxx_battery_voltage(struct mm8xxx_device_info *di,
				   union power_supply_propval *val)
{
	int volt;

	volt = mm8xxx_read(di, MM8XXX_REG_VOLT, false);
	if (volt < 0) {
		dev_err(di->dev, "error reading voltage\n");
		return volt;
	}

	val->intval = volt * 1000;

	return 0;
}

static int mm8xxx_simple_value(int value,
				union power_supply_propval *val)
{
	if (value < 0)
		return value;

	val->intval = value;

	return 0;
}

static int mm8xxx_battery_get_property(struct power_supply *psy,
					enum power_supply_property psp,
					union power_supply_propval *val)
{
	int ret = 0;
	int charge_full = 0;
	int capacity = 0;
	static int last_current = 0;
	int time_to_full = 0;
	struct mm8xxx_device_info *di = power_supply_get_drvdata(psy);
	// bool charging_enabled = true; // 新增：充电使能标志 

	if (di->is_pm_suspend(di)) {
		dev_err(di->dev, "system is suspend can't use i2c\n");
		return -ENODEV;
	}

	mutex_lock(&di->lock);
	if (time_is_before_jiffies(di->last_update + 1 * HZ)) {
		cancel_delayed_work_sync(&di->work);
		mm8xxx_battery_poll(&di->work.work);
	}
	mutex_unlock(&di->lock);

	if (psp != POWER_SUPPLY_PROP_PRESENT && di->cache.flags < 0)
		return -ENODEV;

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		ret = mm8xxx_battery_current_and_status(di, NULL, val);
		// 新增：旁路充电逻辑 - 控制充电状态
        if (di->bypass_setting.enable) {
            if (di->cache.capacity >= di->bypass_setting.high_capacity) {
                // SOC高于高阈值，停止充电
                val->intval = POWER_SUPPLY_STATUS_NOT_CHARGING;
                // charging_enabled = false;
            } else if (di->cache.capacity <= di->bypass_setting.low_capacity) {
                // SOC低于低阈值，恢复充电
                // charging_enabled = true;
            }
        }
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		ret = mm8xxx_battery_voltage(di, val);
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = di->cache.flags < 0 ? 0 : 1;
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		ret = mm8xxx_battery_current_and_status(di, val, NULL);
    // 修复：使用设备私有变量判断
        if (di->bypass_setting.enable && di->cache.capacity >= di->bypass_setting.high_capacity) {
            val->intval = 0; // 强制电流为0
        }
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		ret = mm8xxx_simple_value(di->cache.capacity, val);
		if(val->intval < 2) {
			ret = mm8xxx_battery_voltage(di, val);
			if(ret == 0 && val->intval > 3410000)
				val->intval = 1;
			else
				val->intval = 0;
		}
		break;
	case POWER_SUPPLY_PROP_CAPACITY_LEVEL:
		ret = mm8xxx_battery_capacity_level(di, val);
		break;
	case POWER_SUPPLY_PROP_TEMP:
		ret = mm8xxx_simple_value(di->cache.temperature, val);
		if (ret == 0)
			val->intval -= 2731; /* convert decidegree k to c */
		break;
	case POWER_SUPPLY_PROP_TIME_TO_EMPTY_NOW:
		ret = mm8xxx_simple_value(di->cache.time_to_empty, val);
		break;
	case POWER_SUPPLY_PROP_TIME_TO_EMPTY_AVG:
		ret = mm8xxx_simple_value(di->cache.time_to_empty_avg, val);
		break;
	case POWER_SUPPLY_PROP_TIME_TO_FULL_NOW:
		charge_full = mm8xxx_battery_read_fcc(di);
		capacity = mm8xxx_battery_read_soc(di);
		ret = mm8xxx_battery_current_and_status(di, val, NULL);

		if(((val->intval/1000 - last_current) < 1000)
			&& ((val->intval/1000 - last_current) > -1000)
			&& (last_current > 200))
			time_to_full = (charge_full/1000)* 36* (100 - capacity)/ (val->intval/1000);
		else
			time_to_full = 0;
		last_current = val->intval/1000;
		val->intval = abs(time_to_full);
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		if (di->opts & MM8XXX_O_MUL_CHEM)
			val->intval = POWER_SUPPLY_TECHNOLOGY_UNKNOWN;
		else
			val->intval = POWER_SUPPLY_TECHNOLOGY_LION;
		break;
	case POWER_SUPPLY_PROP_CHARGE_NOW:
		if (di->regs[MM8XXX_REG_NAC] != INVALID_REG_ADDR)
			ret = mm8xxx_simple_value(mm8xxx_battery_read_nac(di), val);
		else
			ret = mm8xxx_simple_value(mm8xxx_battery_read_rc(di), val);
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL:
		ret = mm8xxx_simple_value(di->cache.charge_full, val);
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
		ret = mm8xxx_simple_value(di->charge_design_full, val);
		break;
	/*
	 * TODO: Implement these to make registers set from
	 * power_supply_battery_info visible in sysfs.
	 */
	case POWER_SUPPLY_PROP_ENERGY_FULL_DESIGN:
	case POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN:
		return -EINVAL;
	case POWER_SUPPLY_PROP_CYCLE_COUNT:
		ret = mm8xxx_simple_value(di->cache.cycle_count, val);
		break;
	case POWER_SUPPLY_PROP_ENERGY_NOW:
		ret = mm8xxx_simple_value(di->cache.energy, val);
		break;
	case POWER_SUPPLY_PROP_POWER_AVG:
		ret = mm8xxx_battery_pwr_avg(di, val);
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		ret = mm8xxx_simple_value(di->cache.health, val);
		break;
	case POWER_SUPPLY_PROP_MANUFACTURER:
		val->strval = MM8XXX_MANUFACTURER;
		break;

	/*
	 * TODO: Implement appropriate POWER_SUPPLY_PROP property and read
	 * elapsed time.
	 *
	 *   di->cache.elapsed_months
	 *   di->cache.elapsed_days
	 *   di->cache.elapsed_hours
	 */

	default:
		return -EINVAL;
	}

	return ret;
}

static void mm8xxx_external_power_changed(struct power_supply *psy)
{
	struct mm8xxx_device_info *di = power_supply_get_drvdata(psy);

	cancel_delayed_work_sync(&di->work);
	schedule_delayed_work(&di->work, 0);
}

int mm8xxx_battery_setup(struct mm8xxx_device_info *di)
{
	struct power_supply_desc *psy_desc;
	struct power_supply_config psy_cfg = {
		.of_node = di->dev->of_node,
		.drv_data = di,
	};

	INIT_DELAYED_WORK(&di->work, mm8xxx_battery_poll);
	mutex_init(&di->lock);

	di->regs       = mm8xxx_chip_data[di->chip].regs;
	di->unseal_key = mm8xxx_chip_data[di->chip].unseal_key;
	di->dm_regs    = mm8xxx_chip_data[di->chip].dm_regs;
	di->opts       = mm8xxx_chip_data[di->chip].opts;

	psy_desc = devm_kzalloc(di->dev, sizeof(*psy_desc), GFP_KERNEL);
	if (!psy_desc)
		return -ENOMEM;

	psy_desc->name = di->name;
	psy_desc->type = POWER_SUPPLY_TYPE_UNKNOWN;
	psy_desc->properties = mm8xxx_chip_data[di->chip].props;
	psy_desc->num_properties = mm8xxx_chip_data[di->chip].props_size;
	psy_desc->get_property = mm8xxx_battery_get_property;
	psy_desc->external_power_changed = mm8xxx_external_power_changed;

	di->bat = power_supply_register(di->dev, psy_desc, &psy_cfg);
	if (IS_ERR(di->bat))
		return dev_err_probe(di->dev, PTR_ERR(di->bat),
				     "failed to register battery\n");
	mm8xxx_battery_settings(di);
	mm8xxx_battery_update(di);
	


  

	mutex_lock(&mm8xxx_list_lock);
	list_add(&di->list, &mm8xxx_battery_devices);
	mutex_unlock(&mm8xxx_list_lock);

	return 0;
}
EXPORT_SYMBOL_GPL(mm8xxx_battery_setup);



void mm8xxx_battery_teardown(struct mm8xxx_device_info *di)
{
	/*
	 * power_supply_unregister call mm8xxx_battery_get_property which
	 * call mm8xxx_battery_poll.
	 * Make sure that mm8xxx_battery_poll will not call
	 * schedule_delayed_work again after unregister (which cause OOPS).
	 */
	poll_interval = 0;

	cancel_delayed_work_sync(&di->work);

	power_supply_unregister(di->bat);

	mutex_lock(&mm8xxx_list_lock);
	list_del(&di->list);
	mutex_unlock(&mm8xxx_list_lock);

	mutex_destroy(&di->lock);
}
EXPORT_SYMBOL_GPL(mm8xxx_battery_teardown);

MODULE_AUTHOR("Yasuhiro Kinoshita <ykinoshita.a2@minebeamitsumi.com>");
MODULE_AUTHOR("Takayuki Sugaya <tsugaya.a2@minebeamitsumi.com>");
MODULE_DESCRIPTION("MM8xxx battery monitor driver");
MODULE_LICENSE("GPL");
