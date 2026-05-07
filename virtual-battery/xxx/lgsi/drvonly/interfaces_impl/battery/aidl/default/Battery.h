/**
 * IBattery.aidl impl
 */

#pragma once

#include <android-base/file.h>
#include <utils/Log.h>
#include <aidl/vendor/lenovo/hardware/battery/BnBattery.h>

namespace aidl {
namespace vendor {
namespace lenovo {
namespace hardware {
namespace battery {

using ::android::base::ReadFileToString;
using ::android::base::WriteStringToFile;
using ::std::string;
using ::std::shared_ptr;
using ::ndk::ScopedAStatus;

class Battery : public BnBattery {
public:
    Battery() = default;
    virtual ~Battery();

    /**
     * 充电使能：/sys/class/power_supply/battery/charging_enable            1: charing               0: discharging
     * 供电使能：/sys/class/power_supply/usb/input_suspend               1: dissupply to device   0: supply to the device
     * 快充/float类型：/sys/class/power_supply/usb/real_type                 return the real type string
     * 电池养护1.0：/sys/class/power_supply/battery/battery_maintenance      1: enabled battery maintenance
     * 电池养护2.0：/sys/class/power_supply/battery/battery_maintenance20    1: enabled battery maintenance v2
     * shipping_mode使能：/sys/class/power_supply/battery/shipping_mode      1: into shipping mode
     */
    char const* CHARGING_ENABLED       = "/sys/class/power_supply/battery/charging_enable";
    char const* INPUT_SUSPEND          = "/sys/class/power_supply/usb/input_suspend";
    char const* CHARGING_REAL_TYPE     = "/sys/class/power_supply/usb/real_type";
    char const* BATTERY_MAINTENANCE_V1 = "/sys/class/power_supply/battery/battery_maintenance";
    char const* BATTERY_MAINTENANCE_V2 = "/sys/class/power_supply/battery/battery_maintenance20";
    char const* SHIPPING_MODE_PATH     = "/sys/class/power_supply/usb/ship_mode";
    char const* BAT_ACTIVATE_DATE      = "/sys/devices/platform/11016000.i2c/i2c-5/5-0055/activate_date";
    char const* READ_PDATE_PATH        = "/sys/devices/platform/11016000.i2c/i2c-5/5-0055/produce_date";
    char const* BAT_PROTECTLEVEL_PATH  = "/sys/class/power_supply/battery/battery_protection_setting";
    char const* MAX_CHG_LEVEL_PATH     = "/sys/class/power_supply/battery/battery_protection_setting_eu";
    char const* BAT_RECHGPERCENT_PATH  = "sys/class/power_supply/battery/battery_recharge_setting";
    char const* BAT_SOH_PATH           = "/sys/devices/platform/11016000.i2c/i2c-5/5-0055/soh";
    char const* BAT_CV_PATH   	       = "/sys/class/power_supply/battery/cv";
    char const* BAT_VOLT_NOW_PATH      = "sys/class/power_supply/battery/voltage_now";
    char const* BAT_CURR_NOW_PATH      = "sys/class/power_supply/battery/current_now";
    char const* BAT_TTF_PATH           = "/sys/class/power_supply/battery/time_to_full_now";
    char const* BAT_TTE_PATH           = "/sys/class/power_supply/mm8013c10-0/time_to_empty_now";
    char const* BAT_GPIO_PATH          = "";
    char const* STYLS_CMD_PATH         = "/sys/class/power_supply/battery/cycle_count";

    /**
     * ZUI 16.1 feature
     * the node detail see: http://zwork.lenovo.com.cn:8090/pages/viewpage.action?pageId=37862670
     *
     * battery protection setting: /sys/class/power_supply/battery/protection_setting: ABBCCC
     * battery activate date: /sys/class/power_supply/battery/activate_date  AABBCC: year, month, day
     * battery produced date: /sys/class/power_supply/battery/produce_date   AABBCC: year, month, day
     */
    char const* BP_PROTECTION_SETTING = "/sys/class/power_supply/battery/protection_setting";
    char const* PROTECTION_SETTING_EU = "/sys/class/power_supply/battery/protection_setting_eu";
    char const* RECHARGE_SETTING      = "/sys/class/power_supply/battery/recharge_setting";
    char const* BATTERY_TTF           = "/sys/class/power_supply/battery/time_fo_full_now";
    char const* BATTERY_TTE           = "/sys/class/power_supply/battery/time_fo_empty_now";
    char const* BATTERY_ACTIVATE_DATE      = "/sys/class/qcom-battery/fg1_activate_date";
    char const* BATTERY_ACTIVATE_DATE_NUM2 = "/sys/class/qcom-battery/fg2_activate_date";
    char const* BATTERY_PRODUCED_DATE      = "/sys/class/qcom-battery/fg1_produce_date";
    char const* BATTERY_PRODUCED_DATE_NUM2 = "/sys/class/qcom-battery/fg2_produce_date";
    char const* BATTERY_SOH                = "/sys/class/qcom-battery/fg1_soh";
    char const* BATTERY_SOH_NUM2           = "/sys/class/qcom-battery/fg2_soh";
    char const* BATTERY_CYCLE_COUNT        = "/sys/class/qcom-battery/fg1_cycle_count";
    char const* BATTERY_CYCLE_COUNT_NUM2   = "/sys/class/qcom-battery/fg2_cycle_count";

    // Battery info
    char const* BATTERY_TEMPERATURE      = "/sys/class/qcom-battery/fg1_temp";
    char const* BATTERY_TEMPERATURE_NUM2 = "/sys/class/qcom-battery/fg2_temp";
    char const* BATTERY_VOLTAGE          = "/sys/class/qcom-battery/fg1_voltage";
    char const* BATTERY_VOLTAGE_NUM2     = "/sys/class/qcom-battery/fg2_voltage";
    char const* BATTERY_CURRENT          = "/sys/class/qcom-battery/fg1_current";
    char const* BATTERY_CURRENT_NUM2     = "/sys/class/qcom-battery/fg2_current";

    // QI pen stylus
    char const* QI_PEN_STYLUE = "/sys/class/power_supply/wls_tx/cmd";

    // DEBUG
    char const* BATTERY_CV      = "/sys/class/power_supply/battery/cv";
    char const* BATTERY_CV_NUM2 = "/sys/class/qcom_battery/fg2_cv";
    char const* PORTX_VOLTAGE_1 = "/sys/class/power_supply/usb/voltage_now";
    char const* PORTX_VOLTAGE_2 = "/sys/class/power_supply/wireless/voltage_now";
    char const* PORTX_CURRENT_1 = "/sys/class/power_supply/usb/current_now";
    char const* PORTX_CURRENT_2 = "/sys/class/power_supply/wireless/current_now";
    char const* PORTX_GPIO_DET_1 = "/sys/class/power_supply/usb/det_gpio_port1";
    char const* PORTX_GPIO_DET_2 = "/sys/class/power_supply/usb/det_gpio_port2";

    // Battery Manufacturer
    char const* BATTERY_MANUFACTURER = "/sys/class/qcom-battery/battery1_manufacturer";
    char const* BATTERY_MANUFACTURER_NUM2 = "/sys/class/qcom-battery/battery2_manufacturer";

    // Charger Type
    enum ChargerType {
        ANDORID_CHARGER_NORMAL = 0,
        ANDROID_CHARGER_FAST,
        ANDROID_CHARGER_FLOAT
    };

    ::ndk::ScopedAStatus getBatteryMaintenanceEnabledV2(bool* _aidl_return) override;
    ::ndk::ScopedAStatus getChargerType(std::string* _aidl_return) override;
    ::ndk::ScopedAStatus isBatteryMaintenanceEnabled(bool* _aidl_return) override;
    ::ndk::ScopedAStatus setBatteryChargeDisabled(bool in_enable, bool* _aidl_return) override;
    ::ndk::ScopedAStatus setBatteryMaintenanceEnabled(bool in_enable, bool* _aidl_return) override;
    ::ndk::ScopedAStatus setBatteryMaintenanceEnabledV2(bool in_enable, bool* _aidl_return) override;
    ::ndk::ScopedAStatus setUsbSupplyDisabled(bool in_enable, bool* _aidl_return) override;
    ::ndk::ScopedAStatus setShipModeState(int32_t in_shipMode, bool* _aidl_return) override;

    // zui 16.1
    ::ndk::ScopedAStatus setBatteryActivateDate(const std::string& in_date, bool* _aidl_return) override;
    ::ndk::ScopedAStatus getBatteryActivateDate(std::string* _aidl_return) override;
    ::ndk::ScopedAStatus getBatteryProduceDate(std::string* _aidl_return) override;
    ::ndk::ScopedAStatus setBatteryProtectedLevel(int32_t in_bp_level, bool* _aidl_return) override;
    ::ndk::ScopedAStatus getBatteryProtectedLevel(int32_t* _aidl_return) override;
    ::ndk::ScopedAStatus setMaxBatteryChargingLevel(int32_t in_maxLevel, bool* _aidl_return) override;
    ::ndk::ScopedAStatus getMaxBatteryChargingLevel(int32_t* _aidl_return) override;
    ::ndk::ScopedAStatus setBatteryRechargingPercent(int32_t in_percentage, bool* _aidl_return) override;
    ::ndk::ScopedAStatus getBatteryRechargingPercent(int32_t* _aidl_return) override;
    ::ndk::ScopedAStatus getBatterySOH(int32_t* _aidl_return) override;
    ::ndk::ScopedAStatus getBatteryCV(int32_t* _aidl_return) override;
    ::ndk::ScopedAStatus getPortxVoltageNow(int32_t in_port_id, int32_t* _aidl_return) override;
    ::ndk::ScopedAStatus getPortxCurrentNow(int32_t in_port_id, int32_t* _aidl_return) override;
    ::ndk::ScopedAStatus getPortxGPIO(int32_t in_port_id, bool* _aidl_return) override;
    ::ndk::ScopedAStatus getBatteryTimeToFullNow(int64_t* _aidl_return) override;
    ::ndk::ScopedAStatus getBatteryTimeToEmptyNow(int64_t* _aidl_return) override;

    // Add battery info method
    ::ndk::ScopedAStatus getBatteryTemperature(int32_t* _aidl_return) override;
    ::ndk::ScopedAStatus getBatteryCycleCount(int32_t* _aidl_return) override;
    ::ndk::ScopedAStatus getBatteryVoltage(int32_t* _aidl_return) override;
    ::ndk::ScopedAStatus getBatteryCurrent(int32_t* _aidl_return) override;

    // Add dual battery interface
    ::ndk::ScopedAStatus setBatteryActivateDateForNum(int32_t in_numId, const std::string& in_date, bool* _aidl_return) override;
    ::ndk::ScopedAStatus getBatteryActivateDateForNum(int32_t in_numId, std::string* _aidl_return) override;
    ::ndk::ScopedAStatus getBatteryProduceDateForNum(int32_t in_numId, std::string* _aidl_return) override;
    ::ndk::ScopedAStatus getBatterySOHForNum(int32_t in_numId, int32_t* _aidl_return) override;
    ::ndk::ScopedAStatus getBatteryCVForNum(int32_t in_numId, int32_t* _aidl_return) override;
    ::ndk::ScopedAStatus getBatteryCycleCountForNum(int32_t in_numId, int32_t* _aidl_return) override;
    ::ndk::ScopedAStatus getBatteryVoltageForNum(int32_t in_numId, int32_t* _aidl_return) override;
    ::ndk::ScopedAStatus getBatteryCurrentForNum(int32_t in_numId, int32_t* _aidl_return) override;
    ::ndk::ScopedAStatus getBatteryTemperatureForNum(int32_t in_numId, int32_t* _aidl_return) override;

    // QI stylus pen charging command
    ::ndk::ScopedAStatus setStylusQiCommand(int32_t value, bool* _aidl_return) override;

    // Add dual battery manufacturer interface
    ::ndk::ScopedAStatus getBatteryManufacturerForNum(int32_t in_numId, std::string* _aidl_return) override;
};

} // battery
} // hardware
} // lenovo
} // vendor
} // aidl