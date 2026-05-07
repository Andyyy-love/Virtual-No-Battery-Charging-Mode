/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_MM8XXX_BATTERY_H__
#define __LINUX_MM8XXX_BATTERY_H__

/* 新增：旁路充电相关宏定义（参考联发科代码） */
#define MM8XXX_CMD_LENOVO_SETTING 0xF4 // 旁路参数存储的UserNVM地址
#define MM8XXX_BYPASS_ADDR 10 // 旁路参数在0xF4中的偏移（10~12字节）
#define MM8XXX_BYPASS_LEN 3 // 旁路参数占3字节
#define MM8XXX_BYPASS_UNSEAL_CODE 0x56781234 // 读写NVM的unseal密码（和产线日期一致）

/* 旁路参数格式：enable*100000 + low_capacity*1000 + high_capacity */
#define MM8XXX_BYPASS_ENABLE_SHIFT 5 // enable: 100000倍 = 1<<17 ≈ 100000，简化为*100000
#define MM8XXX_BYPASS_LOW_SHIFT 3 // low_cap: 1000倍
#define MM8XXX_BYPASS_HIGH_MASK 0x3FF // high_cap: 0~100 

enum mm8xxx_chip {
	MM8013C10 = 1,
};

struct mm8xxx_device_info;
struct mm8xxx_dm_reg; 

// 新增：旁路参数结构体
struct mm8xxx_bypass_setting {
    bool enable;
    u16 low_capacity;
    u16 high_capacity;
}; 


struct mm8xxx_access_methods {
	int (*read)(struct mm8xxx_device_info *di, u8 reg, bool single);
	int (*write)(struct mm8xxx_device_info *di, u8 reg, int value, bool single);
	int (*read_bulk)(struct mm8xxx_device_info *di, u8 reg, u8 *data, int len);
	int (*write_bulk)(struct mm8xxx_device_info *di, u8 reg, u8 *data, int len);
};

struct mm8xxx_reg_cache {
	int temperature;
	int time_to_empty;
	int time_to_empty_avg;
	int time_to_full;
	int charge_full;
	int cycle_count;
	int capacity;
	int energy;
	int flags;
	int health;
	int elapsed_months;
	int elapsed_days;
	int elapsed_hours;
};

struct mm8xxx_device_info {
	struct device *dev;
	int id;
	enum mm8xxx_chip chip;
	u32 opts;
	const char *name;
	struct mm8xxx_dm_reg *dm_regs;
	u32 unseal_key;
	struct mm8xxx_access_methods bus;
	struct mm8xxx_reg_cache cache;
	int charge_design_full;
	unsigned long last_update;
	struct delayed_work work;
	struct power_supply *bat;
	struct list_head list;
	struct mutex lock;
	u8 *regs;
	u8 cdev_reg;
	u8 cdev_read_size;
	u32 cdev_unseal_code;
	bool (*is_pm_suspend)(struct mm8xxx_device_info *di);
	
	// 新增：旁路充电参数
    struct mm8xxx_bypass_setting bypass_setting;
};

void mm8xxx_battery_update(struct mm8xxx_device_info *di);
int mm8xxx_battery_setup(struct mm8xxx_device_info *di);
void mm8xxx_battery_teardown(struct mm8xxx_device_info *di);
int mm8xxx_battery_read_soh(struct mm8xxx_device_info *di);
int mm8xxx_battery_read_chargevoltage(struct mm8xxx_device_info *di);
int mm8xxx_battery_write_chargevoltage(struct mm8xxx_device_info *di, u16 cv);

// 新增：旁路充电相关函数声明
int mm8xxx_read_bypass_setting(struct mm8xxx_device_info *di);
int mm8xxx_write_bypass_setting(struct mm8xxx_device_info *di, u32 data);
void mm8xxx_parse_bypass_setting(int data, struct mm8xxx_bypass_setting *setting); 

#endif