// SPDX-License-Identifier: GPL-2.0
/*
 * MM8xxx battery monitor I2C driver
 *
 * Copyright (C) 2023 MITSUMI ELECTRIC CO., LTD. - https://www.mitsumi.co.jp/
 *     Yasuhiro Kinoshita <ykinoshita.a2@minebeamitsumi.com>
 *     Takayuki Sugaya <tsugaya.a2@minebeamitsumi.com>
 */

#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <asm/unaligned.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/jiffies.h>
#include "mm8xxx_battery.h"

static uint32_t suspend_flag = 0;
static struct completion pm_completion;
struct mm8xxx_device_info *_di;

//add by andy
struct mm8xxx_bypass_setting *mm8xxx_get_bypass_data(void)
{
    if (_di)
        return &_di->bypass_setting;
    return NULL;
}
EXPORT_SYMBOL_GPL(mm8xxx_get_bypass_data);

static inline void wait_when_pm_suspend(struct mm8xxx_device_info *di)
{
	//int32_t ret = 0;
	if ( suspend_flag == 1 ) {
		dev_err(di->dev, "suspend_flag is 1, wait for completed");
		/* Waiting for pm resume completed */
		//ret = wait_for_completion_timeout(&pm_completion, msecs_to_jiffies(700));
		//if (!ret) {
		//	dev_err(di->dev, "system(i2c) can't finished resuming procedure.\n");
		//}
	}
}
static bool mm8xxx_is_pm_suspend(struct mm8xxx_device_info *di)
{
	return suspend_flag != 0;
}


static DEFINE_IDR(battery_id);
static DEFINE_MUTEX(battery_mutex);

static irqreturn_t mm8xxx_battery_irq_handler_thread(int irq, void *data)
{
	struct mm8xxx_device_info *di = data;

	mm8xxx_battery_update(di);

	return IRQ_HANDLED;
}

static int mm8xxx_battery_i2c_read(struct mm8xxx_device_info *di, u8 reg,
				    bool single)
{
	struct i2c_client *client = to_i2c_client(di->dev);
	struct i2c_msg msg[2];
	u8 data[2];
	int ret;

	if (!client->adapter)
		return -ENODEV;

	wait_when_pm_suspend(di);

	msg[0].addr = client->addr;
	msg[0].flags = 0;
	msg[0].buf = &reg;
	msg[0].len = sizeof(reg);
	msg[1].addr = client->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].buf = data;
	if (single)
		msg[1].len = 1;
	else
		msg[1].len = 2;

	ret = i2c_transfer(client->adapter, msg, ARRAY_SIZE(msg));
	if (ret < 0) {
		dev_err(di->dev, "whdebug: i2c_transfer failed! ret = %d\n", ret);
		return ret;
	}

	if (!single)
		ret = get_unaligned_le16(data);
	else
		ret = data[0];

	return ret;
}

static int mm8xxx_battery_i2c_write(struct mm8xxx_device_info *di, u8 reg,
				     int value, bool single)
{
	struct i2c_client *client = to_i2c_client(di->dev);
	struct i2c_msg msg;
	u8 data[4];
	int ret;

	if (!client->adapter)
		return -ENODEV;

	wait_when_pm_suspend(di);

	data[0] = reg;
	if (single) {
		data[1] = (u8) value;
		msg.len = 2;
	} else {
		put_unaligned_le16(value, &data[1]);
		msg.len = 3;
	}

	msg.buf = data;
	msg.addr = client->addr;
	msg.flags = 0;

	ret = i2c_transfer(client->adapter, &msg, 1);
	if (ret < 0)
		return ret;
	if (ret != 1)
		return -EINVAL;
	return 0;
}

static int mm8xxx_battery_i2c_bulk_read(struct mm8xxx_device_info *di, u8 reg,
					 u8 *data, int len)
{
	struct i2c_client *client = to_i2c_client(di->dev);
	int ret;

	if (!client->adapter)
		return -ENODEV;

	wait_when_pm_suspend(di);

	ret = i2c_smbus_read_i2c_block_data(client, reg, len, data);
	if (ret < 0)
		return ret;
	if (ret != len)
		return -EINVAL;
	return 0;
}

static int mm8xxx_battery_i2c_bulk_write(struct mm8xxx_device_info *di,
					  u8 reg, u8 *data, int len)
{
	struct i2c_client *client = to_i2c_client(di->dev);
	struct i2c_msg msg;
	u8 buf[33];
	int ret;

	if (!client->adapter)
		return -ENODEV;

	wait_when_pm_suspend(di);

	buf[0] = reg;
	memcpy(&buf[1], data, len);

	msg.buf = buf;
	msg.addr = client->addr;
	msg.flags = 0;
	msg.len = len + 1;

	ret = i2c_transfer(client->adapter, &msg, 1);
	if (ret < 0)
		return ret;
	if (ret != 1)
		return -EINVAL;
	return 0;
}

/*
* This function shows how the Date-Data is to be written.
* See the flowchart "How to read data from UserNVM" in the document "FG-1549-2".
*/
static int mm8xxx_read_date(struct mm8xxx_device_info *di, int target, u32 unseal_code)
{
	int ret;
	u8 data[32];
	int date;

	/*
	* Send Unseal code[0]
	*   Write command 0x00
	*   and Unseal code data0
	*/ 
	data[0] = (u8)(unseal_code & 0xFF);
	data[1] = (u8)((unseal_code >> 8) & 0xFF);
	ret = mm8xxx_battery_i2c_bulk_write(di, 0x00, data, 2);
	if (ret < 0) {
		return -1;
	}
	
	/*
	* Send Unseal code[1]
	*   Write command 0x00
	*   and Unseal code data1
	*/
	data[0] = (u8)((unseal_code >> 16) & 0xFF);
	data[1] = (u8)((unseal_code >> 24) & 0xFF);
	ret = mm8xxx_battery_i2c_bulk_write(di, 0x00, data, 2);
	if (ret < 0) {
		return -1;
	}

	/*
	* User NVM Write Setting
	*   Write command 0x61
	*   and Data 0x00
	*/
	data[0] = (u8)0x00;
	ret = mm8xxx_battery_i2c_bulk_write(di, 0x61, data, 1);
	if (ret < 0) {
		return -1;
	}

	/*
	* Manufacture A/B Request
	*   Write command 0x3E
	*   and Data 0x00F2/0x00F3
	*/
	data[0] = (u8)(target & 0xFF);
	data[1] = (u8)((target >> 8) & 0xFF);
	ret = mm8xxx_battery_i2c_bulk_write(di, 0x3E, data, 2);
	if (ret < 0) {
		return -1;
	}

	/*
	* Read Manufacture A/B Request
	*   Read command 0x40
	*   and get 32bytes data
	*/
	ret = mm8xxx_battery_i2c_bulk_read(di, 0x40, data, 32);
	if (ret < 0) {
		return -1;
	}
	
	date = data[0] & 0xFF;		/* Day:	  [0] */
	date |= (data[1] & 0xFF) << 8;	/* Month: [1] */
	date |= (data[2] & 0xFF) << 16;	/* Year:  [2] */

	/*
	* Seal Set Request
	*   Write command 0x00
	*   and Data 0x0020
	*   -> wait 100msec
	*/
	data[0] = (u8)0x20;
	data[1] = (u8)0x00;
	ret = mm8xxx_battery_i2c_bulk_write(di, 0x00, data, 2);
	msleep(100);
	if (ret < 0) {
		return -1;
	}

	return date;
}

/*
* This function shows how the Date-Data is to be written.
* See the flowchart "How to write data to UserNVM" in the document "FG-1549-2".
*/
static int mm8xxx_write_date(struct mm8xxx_device_info *di, int target, u32 unseal_code, u32 date)
{
	int ret;
	u8 data[32];
	int sum;
	int i;

	/*
	* Send Unseal code[0]
	*   Write command 0x00
	*   and Unseal code data0
	*/
	data[0] = (u8)(unseal_code & 0xFF);
	data[1] = (u8)((unseal_code >> 8) & 0xFF);
	ret = mm8xxx_battery_i2c_bulk_write(di, 0x00, data, 2);
	if (ret < 0) {
		return -1;
	}

	/*
	* Send Unseal code[1]
	*   Write command 0x00
	*   and Unseal code data1
	*/
	data[0] = (u8)((unseal_code >> 16) & 0xFF);
	data[1] = (u8)((unseal_code >> 24) & 0xFF);
	ret = mm8xxx_battery_i2c_bulk_write(di, 0x00, data, 2);
	if (ret < 0) {
		return -1;
	}

	/*
	* User NVM Write Setting
	*   Write command 0x61
	*   and Data 0x00
	*/
	data[0] = (u8)0x00;
	ret = mm8xxx_battery_i2c_bulk_write(di, 0x61, data, 1);
	if (ret < 0) {
		return -1;
	}

	/*
	* Manufacture A/B Request
	*   Write command 0x3E
	*   and Data 0x00F2/0x00F3
	*/
	data[0] = (u8)(target & 0xFF);
	data[1] = (u8)((target >> 8) & 0xFF);
	ret = mm8xxx_battery_i2c_bulk_write(di, 0x3E, data, 2);
	if (ret < 0) {
		return -1;
	}

	/*
	* Write Manufacture A/B data
	*   Write command 0x40
	*   and factory data (any size)
	*/
	data[0] = (u8)(date & 0xFF);		/* Day:   [0] */
	data[1] = (u8)((date >> 8) & 0xFF);	/* Month: [1] */
	data[2] = (u8)((date >> 16) & 0xFF);	/* Year:  [2] */
	ret = mm8xxx_battery_i2c_bulk_write(di, 0x40, data, 3);
	if (ret < 0) {
		return -1;
	}

	/*
	* Read Manufacture A/B Request
	*   Read command 0x40
	*   and get 32bytes data
	*/
	ret = mm8xxx_battery_i2c_bulk_read(di, 0x40, data, 32);
	if (ret < 0) {
		return -1;
	}

	/*
	* Calculate byte checksum of Write Data (32bytes)
	*   Calculate checksum of 32bytes data
	*/
	sum = 0;
	for (i = 0; i < 32; i++) {
		sum += (int)data[i];
	}
	sum &= 0xFF;

	/*
	* Invert checksum data
	*/
	sum = 0xFF - sum;

	/*
	* Write Inverted checksum data
	*   Write command 0x60
	*   and Data <checksum>
	*/
	data[0] = (u8)sum;
	ret = mm8xxx_battery_i2c_bulk_write(di, 0x60, data, 1);
	if (ret < 0) {
		return -1;
	}

	/*
	* Wait for update
	*   Wait 100msec
	*/
	msleep(100);

	/*
	* Seal Set Request
	*   Write command 0x00
	*   and Data 0x0020
	*   -> wait 100msec
	*/
	data[0] = (u8)0x20;
	data[1] = (u8)0x00;
	ret = mm8xxx_battery_i2c_bulk_write(di, 0x00, data, 2);
	msleep(100);
	if (ret < 0) {
		return -1;
	}

	return 0;
}


// ===================== 新增：旁路充电相关函数 =====================
// 解析旁路参数（data格式：enable*100000 + low*1000 + high）
void mm8xxx_parse_bypass_setting(int data, struct mm8xxx_bypass_setting *setting)
{
    if (!setting)
        return;

    setting->enable = (u16)(data / 100000);
    setting->low_capacity = (u16)((data / 1000) % 100);
    setting->high_capacity = (u16)(data % 1000);
    
    dev_dbg(NULL, "Bypass setting parsed: enable=%d, low=%d, high=%d",
        setting->enable, setting->low_capacity, setting->high_capacity);
}

// 读取旁路充电参数（从0xF4的10-12字节）
static int mm8xxx_read_bypass_data(struct mm8xxx_device_info *di, u32 unseal_code)
{
    int ret;
    u8 data[32];
    int bypass_data = 0;
    int i;

    // 1. 发送Unseal码
    data[0] = (u8)(unseal_code & 0xFF);
    data[1] = (u8)((unseal_code >> 8) & 0xFF);
    ret = mm8xxx_battery_i2c_bulk_write(di, 0x00, data, 2);
    if (ret < 0) return -1;

    data[0] = (u8)((unseal_code >> 16) & 0xFF);
    data[1] = (u8)((unseal_code >> 24) & 0xFF);
    ret = mm8xxx_battery_i2c_bulk_write(di, 0x00, data, 2);
    if (ret < 0) return -1;

    // 2. User NVM写设置
    data[0] = 0x00;
    ret = mm8xxx_battery_i2c_bulk_write(di, 0x61, data, 1);
    if (ret < 0) return -1;

    // 3. 选择0xF4地址
    data[0] = (u8)(MM8XXX_CMD_LENOVO_SETTING & 0xFF);
    data[1] = (u8)((MM8XXX_CMD_LENOVO_SETTING >> 8) & 0xFF);
    ret = mm8xxx_battery_i2c_bulk_write(di, 0x3E, data, 2);
    if (ret < 0) return -1;

    // 4. 读取32字节数据
    ret = mm8xxx_battery_i2c_bulk_read(di, 0x40, data, 32);
    if (ret < 0) return -1;

    // 5. 提取10-12字节的旁路参数
    bypass_data = 0;
    for (i = 0; i < MM8XXX_BYPASS_LEN; i++) {
        bypass_data |= (data[MM8XXX_BYPASS_ADDR + i] & 0xFF) << (8 * i);
    }

    // 6. 重新密封
    data[0] = 0x20;
    data[1] = 0x00;
    ret = mm8xxx_battery_i2c_bulk_write(di, 0x00, data, 2);
    msleep(100);
    if (ret < 0) return -1;

    return bypass_data;
}

// 写入旁路充电参数（到0xF4的10-12字节）
static int mm8xxx_write_bypass_data(struct mm8xxx_device_info *di, u32 unseal_code, u32 bypass_data)
{
    int ret;
    u8 data[32];
    int sum;
    int i;

    // 1. 发送Unseal码
    data[0] = (u8)(unseal_code & 0xFF);
    data[1] = (u8)((unseal_code >> 8) & 0xFF);
    ret = mm8xxx_battery_i2c_bulk_write(di, 0x00, data, 2);
    if (ret < 0) return -1;

    data[0] = (u8)((unseal_code >> 16) & 0xFF);
    data[1] = (u8)((unseal_code >> 24) & 0xFF);
    ret = mm8xxx_battery_i2c_bulk_write(di, 0x00, data, 2);
    if (ret < 0) return -1;

    // 2. User NVM写设置
    data[0] = 0x00;
    ret = mm8xxx_battery_i2c_bulk_write(di, 0x61, data, 1);
    if (ret < 0) return -1;

    // 3. 选择0xF4地址
    data[0] = (u8)(MM8XXX_CMD_LENOVO_SETTING & 0xFF);
    data[1] = (u8)((MM8XXX_CMD_LENOVO_SETTING >> 8) & 0xFF);
    ret = mm8xxx_battery_i2c_bulk_write(di, 0x3E, data, 2);
    if (ret < 0) return -1;

    // 4. 读取原有32字节数据
    ret = mm8xxx_battery_i2c_bulk_read(di, 0x40, data, 32);
    if (ret < 0) return -1;

    // 5. 更新10-12字节的旁路参数
    for (i = 0; i < MM8XXX_BYPASS_LEN; i++) {
        data[MM8XXX_BYPASS_ADDR + i] = (u8)(bypass_data >> (8 * i) & 0xFF);
    }

    // 6. 写入32字节数据
    ret = mm8xxx_battery_i2c_bulk_write(di, 0x40, data, 32);
    if (ret < 0) return -1;

    // 7. 重新读取并计算校验和
    ret = mm8xxx_battery_i2c_bulk_read(di, 0x40, data, 32);
    if (ret < 0) return -1;

    sum = 0;
    for (i = 0; i < 32; i++) {
        sum += (int)data[i];
    }
    sum &= 0xFF;
    sum = 0xFF - sum;

    // 8. 写入校验和
    data[0] = (u8)sum;
    ret = mm8xxx_battery_i2c_bulk_write(di, 0x60, data, 1);
    if (ret < 0) return -1;

    msleep(100);

    // 9. 重新密封
    data[0] = 0x20;
    data[1] = 0x00;
    ret = mm8xxx_battery_i2c_bulk_write(di, 0x00, data, 2);
    msleep(100);
    if (ret < 0) return -1;

    return 0;
} 

// 对外暴露的旁路参数读取函数
int mm8xxx_read_bypass_setting(struct mm8xxx_device_info *di)
{
    int ret;
    long unseal_code = MM8XXX_BYPASS_UNSEAL_CODE;

    if (!di) return -EINVAL;

    ret = mm8xxx_read_bypass_data(di, (u32)unseal_code);
    if (ret < 0) {
        dev_err(di->dev, "Failed to read bypass setting");
        return ret;
    }

    // 解析并保存到设备结构体
    mm8xxx_parse_bypass_setting(ret, &di->bypass_setting);
    return ret;
}
EXPORT_SYMBOL_GPL(mm8xxx_read_bypass_setting);

// 对外暴露的旁路参数写入函数
int mm8xxx_write_bypass_setting(struct mm8xxx_device_info *di, u32 data)
{
    int ret;
    long unseal_code = MM8XXX_BYPASS_UNSEAL_CODE;

    if (!di) return -EINVAL;

    ret = mm8xxx_write_bypass_data(di, (u32)unseal_code, data);
    if (ret != 0) {
        dev_err(di->dev, "Failed to write bypass setting");
        return ret;
    }

    // 更新本地缓存
    mm8xxx_parse_bypass_setting(data, &di->bypass_setting);
    return 0;
}
EXPORT_SYMBOL_GPL(mm8xxx_write_bypass_setting); 

// ===================== 新增：Sysfs节点 - 旁路充电 =====================
static ssize_t mm8xxx_get_bypass_setting(struct device *dev,
                   struct device_attribute *attr,
                   char *buf)
{
    struct i2c_client *client = to_i2c_client(dev);
    struct mm8xxx_device_info *di = i2c_get_clientdata(client);
    int ret;

    ret = mm8xxx_read_bypass_setting(di);
    if (ret < 0) return ret;

    return sprintf(buf, "%d\n", ret);
}

static ssize_t mm8xxx_set_bypass_setting(struct device *dev,
                   struct device_attribute *attr,
                   const char *buf,
                   size_t count)
{
    int ret;
    long bypass_data = 0;
    struct i2c_client *client = to_i2c_client(dev);
    struct mm8xxx_device_info *di = i2c_get_clientdata(client);

    ret = kstrtol(buf, 10, &bypass_data);
    if (ret != 0) {
        dev_err(di->dev, "Invalid bypass setting value");
        return -EINVAL;
    }

    ret = mm8xxx_write_bypass_setting(di, (u32)bypass_data);
    if (ret != 0) return ret;

    return strlen(buf);
}




/*
* This function is the entry point of a procedure
* that shows how Production-Date is to be read.
*/
static ssize_t mm8xxx_get_read_production_date(struct device *dev,
				   struct device_attribute *attr,
				   char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct mm8xxx_device_info *di = i2c_get_clientdata(client);
	int ret;
	long unseal_code = 0;
	char *split_work;

	/* Go to UserNVM reading sequence */
	split_work = "56781234";
	ret = kstrtol(split_work, 16, &unseal_code);
	if (ret != 0) {
		dev_err(di->dev, "non-numeric value is set\n");
		return -1;
	}

	unseal_code &= 0xFFFFFFFFL;
	di->cdev_unseal_code = (u32)unseal_code;
	ret = mm8xxx_read_date(di, 0x00F2, di->cdev_unseal_code);
	if (ret < 0) {
		dev_err(di->dev, "failed to read \"PRODUCTION_DATE\"");
		return -1;
	}

	return sprintf(buf, "%06X\n", ret);
}

/*
* This function is the entry point of a procedure
* that shows how Production-Date is to be written.
*/
static ssize_t mm8xxx_write_production_date(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf,
				   size_t count)
{
	int ret;
	long unseal_code = 0;
	long production_date = 0;

	char *split_work, *arg_work;
	struct i2c_client *client = to_i2c_client(dev);
	struct mm8xxx_device_info *di = i2c_get_clientdata(client);

	arg_work = kstrdup(buf, GFP_KERNEL);
	if (!arg_work)
		return -1;

	split_work = "56781234";
	ret = kstrtol(split_work, 16, &unseal_code);
	if (ret != 0) {
		dev_err(di->dev, "non-numeric value is set\n");
		return -1;
	}

	split_work = strsep(&arg_work, ",");
	if (split_work == NULL) {
		dev_err(di->dev, "delimiter not found\n");
		return -1;
	}

	ret = kstrtol(split_work, 16, &production_date);
	if (ret != 0) {
		dev_err(di->dev, "non-numeric value is set\n");
		return -1;
	}

	/* Go to UserNVM writing sequence */
	ret = mm8xxx_write_date(di, 0x00F2, (u32)unseal_code, (u32)production_date);
	if (ret != 0) {
		dev_err(di->dev, "failed to write \"PRODUCTION_DATE\"");
		return -1;
	}

	return strlen(buf);
}

/*
* This function is the entry point of a procedure
* that shows how Start-Date-Of-Use is to be read.
*/
static ssize_t mm8xxx_get_read_start_date_of_use(struct device *dev,
				   struct device_attribute *attr,
				   char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct mm8xxx_device_info *di = i2c_get_clientdata(client);
	int ret;
	char *split_work;
	long unseal_code = 0;

	/* Go to UserNVM reading sequence */
	split_work = "56781234";
	ret = kstrtol(split_work, 16, &unseal_code);
	if (ret != 0) {
		dev_err(di->dev, "non-numeric value is set\n");
		return -1;
	}
	ret = mm8xxx_read_date(di, 0x00F3, di->cdev_unseal_code);
	if (ret < 0) {
		dev_err(di->dev, "failed to read \"START_DATE_OF_USE\"");
		return -1;
	}

	return sprintf(buf, "%06X\n", ret);
}


/*
* This function is the entry point of a procedure
* that shows how Start-Date-Of-Use is to be written.
*/
static ssize_t mm8xxx_write_start_date_of_use(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf,
				   size_t count)
{
	int ret;
	long unseal_code = 0;
	long production_date = 0;

	char *split_work, *arg_work;
	struct i2c_client *client = to_i2c_client(dev);
	struct mm8xxx_device_info *di = i2c_get_clientdata(client);

	arg_work = kstrdup(buf, GFP_KERNEL);
	if (!arg_work)
		return -1;

	split_work = "56781234";
	ret = kstrtol(split_work, 16, &unseal_code);
	if (ret != 0) {
		dev_err(di->dev, "non-numeric value is set\n");
		return -1;
	}

	split_work = strsep(&arg_work, ",");
	if (split_work == NULL) {
		dev_err(di->dev, "delimiter not found\n");
		return -1;
	}

	ret = kstrtol(split_work, 16, &production_date);
	if (ret != 0) {
		dev_err(di->dev, "non-numeric value is set\n");
		return -1;
	}

	/* Go to UserNVM writing sequence */
	ret = mm8xxx_write_date(di, 0x00F3, (u32)unseal_code, (u32)production_date);
	if (ret != 0) {
		dev_err(di->dev, "failed to write \"START_DATE_OF_USE\"");
		return -1;
	}

	return strlen(buf);
}

static ssize_t mm8xxx_show_battery_soh(struct device *dev,
                                    struct device_attribute *attr, char *buf)
{
	int soh = 0;
	struct i2c_client *client = to_i2c_client(dev);
    struct mm8xxx_device_info *di = i2c_get_clientdata(client);

	soh = mm8xxx_battery_read_soh(di);
	if (soh < 0)
		return -1;

	return sprintf(buf, "%d\n", soh);
}

static DEVICE_ATTR(produce_date, 0664, mm8xxx_get_read_production_date, mm8xxx_write_production_date);
static DEVICE_ATTR(activate_date, 0664, mm8xxx_get_read_start_date_of_use, mm8xxx_write_start_date_of_use);
static DEVICE_ATTR(soh, S_IRUGO, mm8xxx_show_battery_soh, NULL);
// 新增：旁路充电Sysfs节点
static DEVICE_ATTR(bypass_setting, 0664, mm8xxx_get_bypass_setting, mm8xxx_set_bypass_setting); 


int battery_show_battery_cv( int *cv)
{
	if(!_di)
		return -1;

	if (suspend_flag) {
		dev_err(_di->dev, "system is suspend can't use i2c\n");
		return -ENODEV;
	}

	*cv = mm8xxx_battery_read_chargevoltage(_di);
	if (*cv < 0)
		return -1;

	return 0;
}
EXPORT_SYMBOL(battery_show_battery_cv);


int battery_set_battery_cv(int *cv) 
{
	int ret = 0;

	if(!_di)
		return -1;

	if (suspend_flag) {
		dev_err(_di->dev, "system is suspend can't use i2c\n");
		return -ENODEV;
	}

	ret = mm8xxx_battery_write_chargevoltage(_di, *cv);
	if (ret < 0) {
		return -1;
	}
	
	return 0;
}
EXPORT_SYMBOL(battery_set_battery_cv);

static int mm8xxx_battery_i2c_probe(struct i2c_client *client)
{
	const struct i2c_device_id *id = i2c_client_get_device_id(client);
	struct mm8xxx_device_info *di;
	int ret;
	char *name;
	int num;

	suspend_flag = 0;
	init_completion(&pm_completion);

	/* Get new ID for the new battery device */
	mutex_lock(&battery_mutex);
	num = idr_alloc(&battery_id, client, 0, 0, GFP_KERNEL);
	mutex_unlock(&battery_mutex);
	if (num < 0)
		return num;

	name = devm_kasprintf(&client->dev, GFP_KERNEL, "%s-%d", id->name, num);
	if (!name)
		goto err_mem;

	di = devm_kzalloc(&client->dev, sizeof(*di), GFP_KERNEL);
	if (!di)
		goto err_mem;

	di->id = num;
	di->dev = &client->dev;
	di->chip = id->driver_data;
	di->name = name;

	di->bus.read = mm8xxx_battery_i2c_read;
	di->bus.write = mm8xxx_battery_i2c_write;
	di->bus.read_bulk = mm8xxx_battery_i2c_bulk_read;
	di->bus.write_bulk = mm8xxx_battery_i2c_bulk_write;
	di->is_pm_suspend = mm8xxx_is_pm_suspend;
	
	dev_err(di->dev, "mm8xxx_battery_i2c_probe name: %s\n", di->name);

	ret = mm8xxx_battery_setup(di);
	if (ret)
		goto err_failed;
	
	/* Schedule a polling after about 1 min */
	schedule_delayed_work(&di->work, 60 * HZ);

	i2c_set_clientdata(client, di);

	if (client->irq) {
		ret = devm_request_threaded_irq(&client->dev, client->irq,
				NULL, mm8xxx_battery_irq_handler_thread,
				IRQF_ONESHOT,
				di->name, di);
		if (ret) {
			dev_err(&client->dev,
				"Unable to register IRQ %d error %d\n",
				client->irq, ret);
			mm8xxx_battery_teardown(di);
			goto err_failed;
		}
	}
	
	di->cdev_reg = 0x0A;	/* flag reg */
	di->cdev_read_size = 1;	/* read size */
	di->cdev_unseal_code = 0x00000000L;
	_di = di;
	device_create_file(&client->dev, &dev_attr_activate_date);
	device_create_file(&client->dev, &dev_attr_produce_date);
	device_create_file(&client->dev, &dev_attr_soh);
	// 新增：创建旁路充电Sysfs节点
    device_create_file(&client->dev, &dev_attr_bypass_setting);

    // 初始化旁路参数
    mm8xxx_read_bypass_setting(di); 
    

	dev_err(di->dev, "mm8xxx_battery_i2c_probe successfully\n");

	return 0;

err_mem:
	ret = -ENOMEM;

err_failed:
	mutex_lock(&battery_mutex);
	idr_remove(&battery_id, num);
	mutex_unlock(&battery_mutex);

	return ret;
}

static void mm8xxx_battery_i2c_remove(struct i2c_client *client)
{
	struct mm8xxx_device_info *di = i2c_get_clientdata(client);

	mm8xxx_battery_teardown(di);

	device_remove_file(&client->dev, &dev_attr_activate_date);
	device_remove_file(&client->dev, &dev_attr_produce_date);
	device_remove_file(&client->dev, &dev_attr_soh);
	// 新增：删除旁路节点
    device_remove_file(&client->dev, &dev_attr_bypass_setting); 

	mutex_lock(&battery_mutex);
	idr_remove(&battery_id, di->id);
	mutex_unlock(&battery_mutex);

	return;
}

static int mm8xxx_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct mm8xxx_device_info *di = i2c_get_clientdata(client);
	suspend_flag = 1;
	reinit_completion(&pm_completion);
	dev_err(di->dev, "suspend enter, suspend_flag = %d\n", suspend_flag);

	return 0;
}

static int mm8xxx_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct mm8xxx_device_info *di = i2c_get_clientdata(client);
	suspend_flag = 0;
	complete(&pm_completion);
	dev_err(di->dev, "resume enter, suspend_flag = %d", suspend_flag);

	return 0;
}
static const struct dev_pm_ops mm8xxx_pm_ops = {
	.suspend = mm8xxx_suspend,
	.resume = mm8xxx_resume,
};

static const struct i2c_device_id mm8xxx_i2c_id_table[] = {
	{ "mm8013c10", MM8013C10 },
	{},
};
MODULE_DEVICE_TABLE(i2c, mm8xxx_i2c_id_table);

#ifdef CONFIG_OF
static const struct of_device_id mm8xxx_battery_i2c_of_match_table[] = {
	{ .compatible = "mitsumi,mm8013C10" },
	{},
};
MODULE_DEVICE_TABLE(of, mm8xxx_battery_i2c_of_match_table);
#endif

static struct i2c_driver mm8xxx_battery_i2c_driver = {
	.driver = {
		.name = "mm8xxx-battery",
		.of_match_table = of_match_ptr(mm8xxx_battery_i2c_of_match_table),
        .pm = &mm8xxx_pm_ops,
	},
	.probe = mm8xxx_battery_i2c_probe,
	.remove = mm8xxx_battery_i2c_remove,
	.id_table = mm8xxx_i2c_id_table,
};
module_i2c_driver(mm8xxx_battery_i2c_driver);

MODULE_AUTHOR("Yasuhiro Kinoshita <ykinoshita.a2@minebeamitsumi.com>");
MODULE_AUTHOR("Takayuki Sugaya <tsugaya.a2@minebeamitsumi.com>");
MODULE_DESCRIPTION("MM8xxx battery monitor i2c driver");
MODULE_LICENSE("GPL");