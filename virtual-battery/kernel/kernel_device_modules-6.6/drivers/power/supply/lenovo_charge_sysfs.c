#include <linux/module.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/debugfs.h>
#include <linux/ctype.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/power_supply.h>
#include <linux/notifier.h>

#include "lenovo_charge_sysfs.h"

static struct sysfs_info_data  *info_data;
static RAW_NOTIFIER_HEAD(sysfs_noitify_chain);

static const char * const POWER_SUPPLY_REAL_TYPE_TEXT[] = {
	[POWER_SUPPLY_USB_TYPE_UNKNOWN]		= "Unknown",
	[POWER_SUPPLY_USB_TYPE_SDP]		= "SDP",
	[POWER_SUPPLY_USB_TYPE_DCP]		= "DCP",
	[POWER_SUPPLY_USB_TYPE_CDP]		= "CDP",
	[POWER_SUPPLY_USB_TYPE_ACA]		= "ACA",
	[POWER_SUPPLY_USB_TYPE_C]		= "HVDCP",
	[POWER_SUPPLY_USB_TYPE_PD]		= "PD", //HVDCP
	[POWER_SUPPLY_USB_TYPE_PD_DRP]		= "PD_DRP",
	[POWER_SUPPLY_USB_TYPE_PD_PPS]		= "PD_PPS",
	[POWER_SUPPLY_USB_TYPE_APPLE_BRICK_ID]	= "USB_FLOAT",
};

static bool lenovo_charger_sysfs_check_psy(void)
{
	if (!(info_data->battery_psy))
		info_data->battery_psy = power_supply_get_by_name("battery");

	if (!(info_data->usb_psy))
		info_data->usb_psy = power_supply_get_by_name("usb");

	if (!(info_data->charger_psy))
		info_data->charger_psy = power_supply_get_by_name("sc-charger");
	if (!(info_data->charger_psy))
		info_data->charger_psy = power_supply_get_by_name("sgm4154x-charger");

	if (!(info_data->battery_psy) || !(info_data->usb_psy) || !(info_data->charger_psy))
		return false;
	else
		return true;
}

static enum power_supply_property usb_props[] = {
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_TYPE,
};

static int lenovo_charge_sysfs_usb_get_prop(struct power_supply *psy, enum power_supply_property prop, union power_supply_propval *val)
{
	int ret = 0;

	switch (prop) {
	case POWER_SUPPLY_PROP_PRESENT:
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = 0;
		break;
	case POWER_SUPPLY_PROP_TYPE:
		val->intval = POWER_SUPPLY_TYPE_USB;
		break;
	default:
		dev_info(&psy->dev, "Unsupported property %d\n", prop);
		return -EINVAL;
	}

	return ret;
}

static const struct power_supply_desc usb_psy_desc = {
	.name = "usb",
	.type = POWER_SUPPLY_TYPE_USB,
	.properties = usb_props,
	.num_properties = ARRAY_SIZE(usb_props),
	.get_property = lenovo_charge_sysfs_usb_get_prop,
};

struct power_supply *lenovo_charge_sysfs_create_usb(struct device *dev)
{
	struct power_supply *psy = NULL;

	psy = devm_power_supply_register(dev, &usb_psy_desc, NULL);
	if (!(psy)) {
		dev_info(dev, "Failed to create usb_psy\n");
		return NULL;
	}

	return psy;
}
EXPORT_SYMBOL(lenovo_charge_sysfs_create_usb);

int lenovo_charge_sysfs_get_property(struct power_supply *psy, enum sysfs_property prop, union power_supply_propval *val)
{
	union power_supply_propval property = {0};

	int cv;

	switch (prop) {
	/* /sys/power_supply/battery */
	case POWER_SUPPLY_PROP_BATTERY_MAINTENANCE:
		val->intval = info_data->is_maintenance;
		break;
	case POWER_SUPPLY_PROP_BATTERY_MAINTENANCE20:
		val->intval = info_data->is_maintenance20;
		break;
	case POWER_SUPPLY_PROP_BATTERY_RECHARGE_SETTING:
		val->intval = info_data->dynamic_soc_rechg_setting;
		break;
	case POWER_SUPPLY_PROP_BATTERY_PROTECTION_SETTING:
		val->intval = info_data->protection_setting;
		break;
	case POWER_SUPPLY_PROP_BATTERY_PROTECTION_SETTING_EU:
		val->intval = info_data->protection_setting_eu;
		break;
	case POWER_SUPPLY_PROP_CHARGING_LEVEL:
		val->intval = info_data->charging_level;
		break;	
	case POWER_SUPPLY_PROP_CHARGING_ENABLE:
		val->intval = info_data->charging_enable;
		break;
	case POWER_SUPPLY_PROP_CV:
		battery_show_battery_cv(&cv);
		if(cv > 0)
			val->intval = cv;
		else
			val->intval = 0;
		break;

	/* /sys/power_supply/usb */
	case POWER_SUPPLY_PROP_REAL_TYPE:
		if (!lenovo_charger_sysfs_check_psy()) {
			info_data->real_type = POWER_SUPPLY_USB_TYPE_UNKNOWN;
		}  else {
			power_supply_get_property(info_data->charger_psy, POWER_SUPPLY_PROP_USB_TYPE, &property);
			info_data->real_type = property.intval;
		}

		if (info_data->real_type > POWER_SUPPLY_USB_TYPE_APPLE_BRICK_ID || info_data->real_type < POWER_SUPPLY_USB_TYPE_UNKNOWN)
			info_data->real_type = POWER_SUPPLY_USB_TYPE_UNKNOWN;

		val->strval = POWER_SUPPLY_REAL_TYPE_TEXT[info_data->real_type];
		break;
	case POWER_SUPPLY_PROP_INPUT_SUSPEND:
		val->intval = info_data->is_input_suspend;
		break;
	case POWER_SUPPLY_PROP_SHIP_MODE:
		val->intval = info_data->entry_ship_mode;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL(lenovo_charge_sysfs_get_property);

int lenovo_charge_sysfs_set_property(struct power_supply *psy, enum sysfs_property prop, const union power_supply_propval *val)
{
	union power_supply_propval property = {0};

	switch (prop) {
	/* /sys/power_supply/battery */
	case POWER_SUPPLY_PROP_BATTERY_MAINTENANCE:
		if(val->intval == 0) {
			info_data->is_maintenance = 0;
			info_data->dynamic_cv_setting = 0;
			info_data->dynamic_gascv_setting = 0;
			break;
		}
		if (lenovo_charger_sysfs_check_psy()) {
			power_supply_get_property(info_data->battery_psy, POWER_SUPPLY_PROP_CYCLE_COUNT, &property);
		}
		/*
		* TODO: Change cycle-counts and voltages according to each devices.
		*/
		if (property.intval < 150) {
			info_data->dynamic_cv_setting = 4530000;
			info_data->dynamic_gascv_setting = 4530;
		} else if ((property.intval >= 150) && (property.intval < 1020)) {
			info_data->dynamic_cv_setting = 4480000;
			info_data->dynamic_gascv_setting = 4470;
		} else if ((property.intval >= 1020) && (property.intval < 1050)) {
			info_data->dynamic_cv_setting = 4432000;
			info_data->dynamic_gascv_setting = 4410;
		} else if (property.intval >= 1050) {
			info_data->dynamic_cv_setting = 4340000;
			info_data->dynamic_gascv_setting = 4310;
		}
		info_data->is_maintenance = 1;
		break;
	case POWER_SUPPLY_PROP_BATTERY_MAINTENANCE20:
		if(val->intval == 0) {
			info_data->is_maintenance20 = 0;
			info_data->dynamic_cv_setting20 = 0;
			info_data->dynamic_gascv_setting20 = 0;
			break;
		}
		info_data->dynamic_cv_setting20 = 4340000;
		info_data->dynamic_gascv_setting20 = 4310;
		info_data->is_maintenance20 = 1;
		break;
	case POWER_SUPPLY_PROP_BATTERY_RECHARGE_SETTING:
		info_data->dynamic_soc_rechg_setting = val->intval;
		break;
	case POWER_SUPPLY_PROP_BATTERY_PROTECTION_SETTING:
		info_data->protection_setting = val->intval;
		raw_notifier_call_chain(&sysfs_noitify_chain, (info_data->protection_setting == 140060), NULL);
		break;
	case POWER_SUPPLY_PROP_BATTERY_PROTECTION_SETTING_EU:
		info_data->protection_setting_eu = val->intval;
		raw_notifier_call_chain(&sysfs_noitify_chain, 0, NULL);
		break;
	case POWER_SUPPLY_PROP_CHARGING_LEVEL:
		info_data->charging_level = val->intval;
		break;	
	case POWER_SUPPLY_PROP_CHARGING_ENABLE:
		info_data->charging_enable = val->intval;
		break;
	case POWER_SUPPLY_PROP_CV:
		break;

	/* /sys/power_supply/usb */
	case POWER_SUPPLY_PROP_REAL_TYPE:
		break;
	case POWER_SUPPLY_PROP_INPUT_SUSPEND:
		if (val->intval == 1)
			info_data->is_input_suspend = 1;
		else if (val->intval == 0)
			info_data->is_input_suspend = 0;
		else
			info_data->is_input_suspend = 0;
		raw_notifier_call_chain(&sysfs_noitify_chain, 0, NULL);
		break;
	case POWER_SUPPLY_PROP_SHIP_MODE:
		info_data->entry_ship_mode = val->intval;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL(lenovo_charge_sysfs_set_property);

int get_battery_protection_setting(void)
{
	return info_data->protection_setting;
}
EXPORT_SYMBOL(get_battery_protection_setting);

int maintenance_set(int *dynamic_cv, int *dynamic_soc_rechg, int *dynamic_gas_cv)
{
	*dynamic_cv = info_data->dynamic_cv_setting;
	*dynamic_soc_rechg = info_data->dynamic_soc_rechg_setting;
	*dynamic_gas_cv = info_data->dynamic_gascv_setting;
	return info_data->is_maintenance;
}
EXPORT_SYMBOL(maintenance_set);

int maintenance20_set(int *dynamic20_cv, int *dynamic_soc_rechg, int *dynamic20_gas_cv)
{
	*dynamic20_cv = info_data->dynamic_cv_setting20;
	*dynamic_soc_rechg = info_data->dynamic_soc_rechg_setting;
	*dynamic20_gas_cv = info_data->dynamic_gascv_setting20;
	return info_data->is_maintenance20;
}
EXPORT_SYMBOL(maintenance20_set);

int get_input_suspend(void)
{
	return info_data->is_input_suspend;
}
EXPORT_SYMBOL(get_input_suspend);

int get_battery_protection_setting_eu(void)
{
	return info_data->protection_setting_eu;
}
EXPORT_SYMBOL(get_battery_protection_setting_eu);

int get_charging_enable(void)
{
	return info_data->charging_enable;
}
EXPORT_SYMBOL(get_charging_enable);

int get_charging_level(void)
{
	return info_data->charging_level;
}
EXPORT_SYMBOL(get_charging_level);

int get_entry_ship_mode(void)
{
	return info_data->entry_ship_mode;
}
EXPORT_SYMBOL(get_entry_ship_mode);

void sysfs_register_notifier(struct notifier_block *nb)
{
	raw_notifier_chain_register(&sysfs_noitify_chain, nb);
}
EXPORT_SYMBOL_GPL(sysfs_register_notifier);
 
void sysfs_unregister_notifier(struct notifier_block *nb)
{
	raw_notifier_chain_unregister(&sysfs_noitify_chain, nb);
}
EXPORT_SYMBOL_GPL(sysfs_unregister_notifier);

static ssize_t lenovo_charge_sysfs_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct power_supply *psy = NULL;
	struct sysfs_info *sysfs_attr;
	union power_supply_propval pval = {0};
	int val;
	ssize_t ret;

	ret = kstrtos32(buf, 0, &val);
	if (ret < 0)
		return ret;

	pval.intval = val;

	psy = dev_get_drvdata(dev);
	if (!psy) {
		dev_info(&psy->dev, "invalid psy");
		return ret;
	}

	sysfs_attr = container_of(attr, struct sysfs_info, attr);
	lenovo_charge_sysfs_set_property(psy, sysfs_attr->prop, &pval);

	return count;
}

static ssize_t lenovo_charge_sysfs_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct power_supply *psy = NULL;
	struct sysfs_info *sysfs_attr;
	union power_supply_propval pval = {0};
	ssize_t count = 0;

	psy = dev_get_drvdata(dev);
	if (!psy) {
		dev_info(&psy->dev, "invalid psy");
		return count;
	}

	sysfs_attr = container_of(attr, struct sysfs_info, attr);
	lenovo_charge_sysfs_get_property(psy, sysfs_attr->prop, &pval);
	if (sysfs_attr->prop == POWER_SUPPLY_PROP_REAL_TYPE)
		count = scnprintf(buf, PAGE_SIZE, "%s\n", pval.strval);
	else
		count = scnprintf(buf, PAGE_SIZE, "%d\n", pval.intval);

	return count;
}

static struct sysfs_info battery_sysfs_field_tbl[] = {
	SYSFS_FIELD_RW(battery_maintenance, POWER_SUPPLY_PROP_BATTERY_MAINTENANCE, false),
	SYSFS_FIELD_RW(battery_maintenance20, POWER_SUPPLY_PROP_BATTERY_MAINTENANCE20, false),
	SYSFS_FIELD_RW(battery_recharge_setting, POWER_SUPPLY_PROP_BATTERY_RECHARGE_SETTING, false),
	SYSFS_FIELD_RW(battery_protection_setting, POWER_SUPPLY_PROP_BATTERY_PROTECTION_SETTING, false),
	SYSFS_FIELD_RW(battery_protection_setting_eu, POWER_SUPPLY_PROP_BATTERY_PROTECTION_SETTING_EU, false),
	SYSFS_FIELD_RW(charging_level, POWER_SUPPLY_PROP_CHARGING_LEVEL, false),
	SYSFS_FIELD_RW(charging_enable, POWER_SUPPLY_PROP_CHARGING_ENABLE, false),
	SYSFS_FIELD_RO(cv, POWER_SUPPLY_PROP_CV, false),
};

static struct attribute
	*battery_sysfs_attrs[ARRAY_SIZE(battery_sysfs_field_tbl) + 1];

static const struct attribute_group battery_sysfs_attr_group = {
	.attrs = battery_sysfs_attrs,
};

static struct sysfs_info usb_sysfs_field_tbl[] = {
	SYSFS_FIELD_RO(real_type, POWER_SUPPLY_PROP_REAL_TYPE, false),
	SYSFS_FIELD_RW(input_suspend, POWER_SUPPLY_PROP_INPUT_SUSPEND, false),
	SYSFS_FIELD_RW(ship_mode, POWER_SUPPLY_PROP_SHIP_MODE, false),
};

static struct attribute *usb_sysfs_attrs[ARRAY_SIZE(usb_sysfs_field_tbl) + 1];

static const struct attribute_group usb_sysfs_attr_group = {
	.attrs = usb_sysfs_attrs,
};

void lenovo_charge_create_sysfs(struct power_supply *psy)
{
	int i = 0, length = 0, rc = 0;

	if (!(psy)) {
		dev_info(&psy->dev, "Invalid psy\n");
		return;
	}	

	if(!info_data)
		info_data = kzalloc(sizeof(*info_data), GFP_KERNEL);

	if (!strcmp(psy->desc->name, "battery")) {
		length = ARRAY_SIZE(battery_sysfs_field_tbl);
		for (i = 0; i < length; i++)
		battery_sysfs_attrs[i] = &battery_sysfs_field_tbl[i].attr.attr;

		battery_sysfs_attrs[length] = NULL;
		rc = sysfs_create_group(&psy->dev.kobj, &battery_sysfs_attr_group);
		if (rc) {
			dev_info(&psy->dev, "Failed to create battery_sysfs\n");
			return;
		}
		if(info_data)
			info_data->battery_psy = psy;
	} else if (!strcmp(psy->desc->name, "usb")) {
		length = ARRAY_SIZE(usb_sysfs_field_tbl);
		for (i = 0; i < length; i++)
			usb_sysfs_attrs[i] = &usb_sysfs_field_tbl[i].attr.attr;

		usb_sysfs_attrs[length] = NULL;
		rc = sysfs_create_group(&psy->dev.kobj, &usb_sysfs_attr_group);
		if (rc) {
			dev_info(&psy->dev, "Failed to create usb_sysfs\n");
			return;
		}
		if(info_data)
			info_data->usb_psy = psy;
	} else {
		/* To do */
	}

	return;
}
EXPORT_SYMBOL(lenovo_charge_create_sysfs);

int lenovo_charge_sysfs_uevent(struct power_supply *psy)
{
	/* To do */

	return 0;
}
EXPORT_SYMBOL(lenovo_charge_sysfs_uevent);

MODULE_AUTHOR("chenyc16@lenovo.com");
MODULE_DESCRIPTION("Lenovo charge sysfs");
MODULE_LICENSE("GPL");