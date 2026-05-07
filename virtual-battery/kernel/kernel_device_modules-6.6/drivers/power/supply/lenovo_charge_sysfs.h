#include <linux/module.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/debugfs.h>
#include <linux/ctype.h>
#include <linux/err.h>
#include <linux/slab.h>

#define MAX_UEVENT_PROP_NUM	20

#define SYSFS_FIELD_RW(_name, _prop, _report)							\
{												\
	.attr = __ATTR(_name, 0644, lenovo_charge_sysfs_show, lenovo_charge_sysfs_store),	\
	.prop = _prop,										\
	.report_uevent = _report,								\
}

#define SYSFS_FIELD_RO(_name, _prop, _report)				\
{									\
	.attr = __ATTR(_name, 0444, lenovo_charge_sysfs_show, NULL),	\
	.prop = _prop,							\
	.report_uevent = _report,					\
}

enum sysfs_property {
	/* Nodes under power_supply/battery/ */
	POWER_SUPPLY_PROP_BATTERY_MAINTENANCE = 0,
	POWER_SUPPLY_PROP_BATTERY_MAINTENANCE20,
	POWER_SUPPLY_PROP_BATTERY_RECHARGE_SETTING,
	POWER_SUPPLY_PROP_BATTERY_PROTECTION_SETTING,
	POWER_SUPPLY_PROP_BATTERY_PROTECTION_SETTING_EU,
	POWER_SUPPLY_PROP_CHARGING_LEVEL,
	POWER_SUPPLY_PROP_CHARGING_ENABLE,
	POWER_SUPPLY_PROP_CV,

	/* Nodes under power_supply/usb/ */
	POWER_SUPPLY_PROP_REAL_TYPE,
	POWER_SUPPLY_PROP_INPUT_SUSPEND,
	POWER_SUPPLY_PROP_SHIP_MODE,
};

struct sysfs_info {
	struct device_attribute attr;
	enum sysfs_property prop;
	bool report_uevent;
};

struct sysfs_info_data {
	struct power_supply *battery_psy;
	struct power_supply *usb_psy;
	struct power_supply *charger_psy;
	int real_type;
	int is_maintenance;
	int is_maintenance20;
	int protection_setting;
	int is_input_suspend;
	int protection_setting_eu;
	int dynamic_cv_setting;
	int dynamic_gascv_setting;
	int dynamic_cv_setting20;
	int dynamic_gascv_setting20;
	int dynamic_soc_rechg_setting;
	int charging_enable;
	int charging_level;
	int entry_ship_mode;
};

extern int battery_show_battery_cv(int *cv);