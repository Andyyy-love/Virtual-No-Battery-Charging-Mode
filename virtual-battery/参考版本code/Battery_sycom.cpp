#include <stdio.h>

#include <android-base/logging.h>
#include <android-base/strings.h>
#include <android-base/parseint.h>
#include <android/binder_manager.h>
#include <cutils/properties.h>

#include <errno.h>
#include <fcntl.h>

#include "Battery.h"

using ::std::string;

namespace aidl {
namespace vendor {
namespace lenovo {
namespace hardware {
namespace battery {

static bool DEBUG = false;

Battery::Battery() {
    char log_value[PROPERTY_VALUE_MAX];
    property_get("persist.debug.zui_battery_debug", log_value, "false");
    LOG(ERROR) << "DEBUG is " << log_value;
    if (strcmp(log_value, "true") == 0 || strcmp(log_value, "1") == 0) {
        DEBUG = true;
    }
}

Battery::~Battery() {}

template <typename T>
struct SupplyTypeMap {
    const char* s;
    T val;
};

template <typename T>
static T mapSupplyString(const char* str, SupplyTypeMap<T> map[]) {
    for (int32_t i = 0; map[i].s; ++i) {
        if (!strcmp(str, map[i].s)) return map[i].val;
    }

    return Battery::ANDORID_CHARGER_NORMAL;
}

/* Sycamore code for SYCAMORE-337 by zhoujj21 at 20241125 start */
int32_t readPowerSupplyType(const string& str) {
    static SupplyTypeMap<int> supplyTypeMap[] {
        {"Unknown", Battery::ANDORID_CHARGER_NORMAL},
        {"PD_PPS", Battery::ANDROID_CHARGER_FAST},
        {"PD", Battery::ANDROID_CHARGER_FAST},
        {"ACA", Battery::ANDROID_CHARGER_FAST},
        {"SDP", Battery::ANDORID_CHARGER_NORMAL},
        {"CDP", Battery::ANDORID_CHARGER_NORMAL},
        {"DCP", Battery::ANDORID_CHARGER_NORMAL},
        {"SCP", Battery::ANDORID_CHARGER_NORMAL},
        {"BrickID", Battery::ANDROID_CHARGER_FLOAT},
        {NULL, 0},
    };
    return mapSupplyString(str.c_str(), supplyTypeMap);
}
/* Sycamore code for SYCAMORE-337 by zhoujj21 at 20241125 end */

static inline int32_t readFromFile(const string& path, std::string* buf) {
    buf->clear();
    if (android::base::ReadFileToString(path.c_str(), buf)) {
        *buf = android::base::Trim(*buf);
    }
    return buf->length();
}

static inline bool writeToFile(const string& path, const string in_value) {
    return android::base::WriteStringToFile(in_value, path.c_str());
}

static inline bool writeToFile(const string& path, int32_t in_value) {
    return android::base::WriteStringToFile(std::to_string(in_value), path.c_str());
}

static inline int32_t getIntField(const string& path) {
    std::string buf;
    int32_t value = -1;

    if (readFromFile(path, &buf) >= 0)
        android::base::ParseInt(buf, &value);
    else
        LOG(ERROR) << path << " get failed! " << buf;

    return value;
}

static inline int32_t readFromFile(const string& path, int32_t* value) {
    *value = getIntField(path);
    return 0;
}

::ndk::ScopedAStatus Battery::getBatteryMaintenanceEnabledV2(bool* _aidl_return) {
    int32_t ret = getIntField(Battery::BATTERY_MAINTENANCE_V2);

    if (ret < 0) {
        LOG(ERROR) << "Battery get Battery MaintenanceV2 failed! ";
    } else {
        *_aidl_return = (ret == 1);
        // LOG(WARNING) << "Battery get Battery MaintenanceV2: " << *_aidl_return;
    }
    return ScopedAStatus::ok();
}

::ndk::ScopedAStatus Battery::getChargerType(std::string* _aidl_return) {
    std::string buf;
    int32_t ret = readFromFile(Battery::CHARGING_REAL_TYPE, &buf);

    if (ret < 0) {
        LOG(ERROR) << "get battery ChargerType failed! ";
    } else {
        *_aidl_return = buf;
        if (DEBUG) {
            LOG(WARNING) << "zjj--get battery ChargerType: " << *_aidl_return;
        }
    }
    return ScopedAStatus::ok();
}

::ndk::ScopedAStatus Battery::isBatteryMaintenanceEnabled(bool* _aidl_return) {
    int32_t ret = getIntField(Battery::BATTERY_MAINTENANCE_V1);

    if (ret < 0) {
        LOG(ERROR) << "get Battery Maintenance v1 status failed! ";
    } else {
        *_aidl_return = (ret == 1);
        LOG(WARNING) << "get Battery Maintenance v1 status: " << ret;
    }
    return ScopedAStatus::ok();
}

::ndk::ScopedAStatus Battery::setBatteryMaintenanceEnabled(bool in_enable, bool* _aidl_return)  {
    *_aidl_return = writeToFile(Battery::BATTERY_MAINTENANCE_V1, in_enable);

    if (!*_aidl_return) {
        LOG(ERROR) << "set BatteryMaintenance v1 status failed";
    } else {
        LOG(WARNING) << "set BatteryMaintenance v1 status : " << in_enable;
    }
    return ScopedAStatus::ok();
}

::ndk::ScopedAStatus Battery::setBatteryMaintenanceEnabledV2(bool in_enable, bool* _aidl_return)  {
    *_aidl_return = writeToFile(Battery::BATTERY_MAINTENANCE_V2, in_enable);

    if (!*_aidl_return) {
        LOG(ERROR) << "set BatteryMaintenance v2 status failed";
    } else {
        LOG(WARNING) << "set batteryMaintenance v2 status : " << in_enable;
    }
    return ScopedAStatus::ok();
}

/**
 * @param in_enabled: false: dis charging to battery (dis_charging); true: charging to battery,
 *                    because the charging_enabeld default value is 1.
 * @param _aidl_return: true if write the node succ; false if write the node flase.
 */
::ndk::ScopedAStatus Battery::setBatteryChargeDisabled(bool in_enable, bool* _aidl_return)  {
    *_aidl_return = writeToFile(Battery::CHARGING_ENABLED, !in_enable);

    if (!*_aidl_return) {
        LOG(ERROR) << "set Battery Charge disabled status failed!";
    } else {
        LOG(WARNING) << "set Battery Charge disabled status: " << in_enable;
    }
    return ScopedAStatus::ok();
}

/**
 * @param in_enabled: true: dis supply to the device (dis_input); false: reply supply.
 * @param _aidl_return: true if write the node succ; false if write the node flase.
 */
::ndk::ScopedAStatus Battery::setUsbSupplyDisabled(bool in_enable, bool* _aidl_return)  {
    *_aidl_return = writeToFile(Battery::INPUT_SUSPEND, in_enable);

    if (!*_aidl_return) {
        LOG(ERROR) << "set Usb Supply disabled status failed!";
    } else {
        LOG(WARNING) << "set Usb Supply disabled status: " << in_enable;
    }
    return ScopedAStatus::ok();
}

::ndk::ScopedAStatus Battery::setShipModeState(int32_t in_shipMode, bool* _aidl_return) {
    if (in_shipMode == 1) {
        // 5526789 is a shipping mode magic number, only this value can enabled this function.
        // In LGSIU 16.1 or Android V, need to write 1 to this node.
        *_aidl_return = writeToFile(Battery::SHIPPING_MODE_PATH, 1 /*5526789*/);
    } else {
        *_aidl_return = false;
    }

    if (!*_aidl_return) {
        LOG(ERROR) << "set Shipping Mode state failed!";
    } else {
        LOG(WARNING) << "set Shipping Mode : " << in_shipMode;
    }
    return ScopedAStatus::ok();
}

::ndk::ScopedAStatus Battery::setBatteryActivateDate(const std::string& in_date, bool* _aidl_return) {
    return setBatteryActivateDateForNum(1, in_date, _aidl_return);
}

::ndk::ScopedAStatus Battery::setBatteryActivateDateForNum(const int32_t numId, const std::string& in_date, bool* _aidl_return) {
    LOG(ERROR) << "set Battery Activate Date: " << in_date << ", num = " << numId;
    if (numId == 1) {
        *_aidl_return = writeToFile(Battery::BATTERY_ACTIVATE_DATE, in_date);
    } else if (numId == 2) {
        *_aidl_return = writeToFile(Battery::BATTERY_ACTIVATE_DATE_NUM2, in_date);
    }
    return ScopedAStatus::ok();
}

::ndk::ScopedAStatus Battery::getBatteryActivateDate(std::string* _aidl_return) {
    return getBatteryActivateDateForNum(1, _aidl_return);
}

::ndk::ScopedAStatus Battery::getBatteryActivateDateForNum(int32_t numId, std::string* _aidl_return) {
    int32_t ret = -1;
    if (numId == 1) {
        ret = readFromFile(Battery::BATTERY_ACTIVATE_DATE, _aidl_return);
    } else if (numId == 2) {
        ret = readFromFile(Battery::BATTERY_ACTIVATE_DATE_NUM2, _aidl_return);
    }

    if (ret < 0) {
        LOG(ERROR) << "get Battery Activate Date failed! ";
    }

    if (DEBUG) {
        LOG(WARNING) << "get Battery Activate Date: " << *_aidl_return;
    }

    return ScopedAStatus::ok();
}

::ndk::ScopedAStatus Battery::getBatteryProduceDate(std::string* _aidl_return) {
    return getBatteryProduceDateForNum(1, _aidl_return);
}

::ndk::ScopedAStatus Battery::getBatteryProduceDateForNum(int32_t numId, std::string* _aidl_return) {
    int32_t ret = -1;
    if (numId == 1) {
        ret = readFromFile(Battery::BATTERY_PRODUCED_DATE, _aidl_return);
    } else if (numId == 2) {
        ret = readFromFile(Battery::BATTERY_PRODUCED_DATE_NUM2, _aidl_return);
    }

    if (ret < 0) {
        LOG(ERROR) << "get Battery Produce Date failed! ";
    }
    if (DEBUG) {
        LOG(WARNING) << "get Battery Produce Date: " << *_aidl_return;
    }

    return ScopedAStatus::ok();
}

::ndk::ScopedAStatus Battery::setBatteryProtectedLevel(int32_t in_bp_level, bool* _aidl_return) {
    LOG(WARNING) << "set Battery protected : " << in_bp_level;
    *_aidl_return = writeToFile(Battery::BP_PROTECTION_SETTING, in_bp_level);

    return ScopedAStatus::ok();
}

::ndk::ScopedAStatus Battery::getBatteryProtectedLevel(int32_t* _aidl_return) {
    int32_t ret = readFromFile(Battery::BP_PROTECTION_SETTING, _aidl_return);

    if (ret < 0) {
        LOG(ERROR) << "get Battery Protected num failed! ";
    }
    if (DEBUG) {
        LOG(WARNING) << "get Battery Protected num: " << *_aidl_return;
    }

    return ScopedAStatus::ok();
}

::ndk::ScopedAStatus Battery::setMaxBatteryChargingLevel(int32_t in_maxLevel, bool* _aidl_return) {
    LOG(WARNING) << "set Max Battery Charging Level: " << in_maxLevel;
    *_aidl_return = writeToFile(Battery::PROTECTION_SETTING_EU, in_maxLevel);

    return ScopedAStatus::ok();
}

::ndk::ScopedAStatus Battery::getMaxBatteryChargingLevel(int32_t* _aidl_return) {
    int32_t ret = readFromFile(Battery::PROTECTION_SETTING_EU, _aidl_return);

    if (ret < 0) {
        LOG(ERROR) << "get Max Battery Charging Level failed! ";
    }
    if (DEBUG) {
        LOG(WARNING) << "get Max Battery Charging Level: " << *_aidl_return;
    }
    return ScopedAStatus::ok();
}

::ndk::ScopedAStatus Battery::setBatteryRechargingPercent(int32_t in_percentage, bool* _aidl_return) {
    LOG(WARNING) << "set Battery Recharging Percentage: " << in_percentage;
    *_aidl_return = writeToFile(Battery::RECHARGE_SETTING, in_percentage);

    return ScopedAStatus::ok();
}

::ndk::ScopedAStatus Battery::getBatteryRechargingPercent(int32_t* _aidl_return) {
    int32_t ret = readFromFile(Battery::RECHARGE_SETTING, _aidl_return);

    if (ret < 0) {
        LOG(ERROR) << "get Battery Recharging Percentage failed! ";
    }
    if (DEBUG) {
        LOG(WARNING) << "get Battery Recharging Percentage: " << *_aidl_return;
    }
    return ScopedAStatus::ok();

}

::ndk::ScopedAStatus Battery::getBatterySOH(int32_t* _aidl_return) {
    return getBatterySOHForNum(1, _aidl_return);
}

::ndk::ScopedAStatus Battery::getBatterySOHForNum(int32_t numId, int32_t* _aidl_return) {
    int32_t ret = 0;
    if (numId == 1) {
        ret = readFromFile(Battery::BATTERY_SOH, _aidl_return);
    } else if (numId == 2) {
        ret = readFromFile(Battery::BATTERY_SOH_NUM2, _aidl_return);
    }

    // get soh failed, so return 100.
    if (*_aidl_return < 0) {
        if (DEBUG) LOG(ERROR) << "Battery::Battery soh < 0, so return 100";
        *_aidl_return = 100;
    }

    if (ret < 0) {
        LOG(ERROR) << "Battery::Battery soh get failed! ";
    } else {
        LOG(WARNING) << "Battery soh is: " << *_aidl_return << ", num = " << numId;
    }
    return ScopedAStatus::ok();
}

::ndk::ScopedAStatus Battery::getBatteryCV(int32_t* _aidl_return) {
    return getBatteryCVForNum(1, _aidl_return);
}

::ndk::ScopedAStatus Battery::getBatteryCVForNum(int32_t numId, int32_t* _aidl_return) {
    int32_t ret = -1;
    if (numId == 1) {
        ret = readFromFile(Battery::BATTERY_CV, _aidl_return);
    } else if (numId == 2) {
        ret = readFromFile(Battery::BATTERY_CV_NUM2, _aidl_return);
    }

    if (ret < 0) {
        LOG(ERROR) << "get battery cv failed! ";
    } else {
        LOG(WARNING) << "Battery cv is: " << *_aidl_return << ", numId = " << numId;
    }
    return ScopedAStatus::ok();

}

::ndk::ScopedAStatus Battery::getBatteryCycleCountForNum(int32_t in_numId, int32_t* _aidl_return) {
    int32_t ret = -1;
    if (in_numId == 1) {
        ret = readFromFile(Battery::BATTERY_CYCLE_COUNT, _aidl_return);
    } else if (in_numId == 2) {
        ret = readFromFile(Battery::BATTERY_CYCLE_COUNT_NUM2, _aidl_return);
    }

    if (ret < 0) {
        LOG(ERROR) << "get battery cycle count failed! ";
        *_aidl_return = 0;
    } else {
        LOG(WARNING) << "Battery cycle_count is: " << *_aidl_return << ", numId = " << in_numId;
    }
    return ScopedAStatus::ok();
}

::ndk::ScopedAStatus Battery::getPortxVoltageNow(int32_t in_port_id, int32_t* _aidl_return) {
    int32_t ret = 0;

    if (in_port_id == 1) {
        ret = readFromFile(Battery::PORTX_VOLTAGE_1, _aidl_return);
    } else if (in_port_id ==2) {
        ret = readFromFile(Battery::PORTX_VOLTAGE_2, _aidl_return);
    } else {
        LOG(ERROR) << "voltage: the port id " << in_port_id << " is error!";
    }

    if (ret < 0) {
        LOG(ERROR) << "portx voltage get failed!";
    }
    if (DEBUG) {
        LOG(WARNING) << "battery portx voltage get succ" << *_aidl_return;
    }
    return ScopedAStatus::ok();
}

::ndk::ScopedAStatus Battery::getPortxCurrentNow(int32_t in_port_id, int32_t* _aidl_return) {
    int32_t ret = 0;

    if (in_port_id == 1) {
        ret = readFromFile(Battery::PORTX_CURRENT_1, _aidl_return);
    } else if (in_port_id ==2) {
        ret = readFromFile(Battery::PORTX_CURRENT_2, _aidl_return);
    } else {
        LOG(ERROR) << "the port id " << in_port_id << " is error!";
    }


    if (ret < 0) {
        LOG(ERROR) << "portx current get failed!";
    }
    if (DEBUG) {
        LOG(WARNING) << "battery portx current get succ" << *_aidl_return;
    }
    return ScopedAStatus::ok();
}

::ndk::ScopedAStatus Battery::getPortxGPIO(int32_t in_port_id, bool* _aidl_return) {
    int32_t ret = 0;

    if (in_port_id == 1) {
        readFromFile(Battery::PORTX_GPIO_DET_1, &ret);
    } else if (in_port_id ==2) {
        readFromFile(Battery::PORTX_GPIO_DET_2, &ret);
    } else {
        LOG(ERROR) << "the gpio port id " << in_port_id << " is error!";
    }

    *_aidl_return = ret > 0 ? true : false;
    LOG(WARNING) << "battery gpio portx get succ" << *_aidl_return;
    return ScopedAStatus::ok();
}

::ndk::ScopedAStatus Battery::getBatteryTimeToFullNow(int64_t* _aidl_return) {
    int32_t tmp = 0;
    int32_t ret = readFromFile(Battery::BATTERY_TTF, &tmp);
    *_aidl_return = tmp;

    if (ret < 0) {
        LOG(ERROR) << "battery ttf get failed!";
    }
    if (DEBUG) {
        LOG(WARNING) << "battery ttf get succ" << *_aidl_return;
    }
    return ScopedAStatus::ok();
}

::ndk::ScopedAStatus Battery::getBatteryTimeToEmptyNow(int64_t* _aidl_return) {
    int32_t tmp = 0;
    int32_t ret = readFromFile(Battery::BATTERY_TTE, &tmp);
    *_aidl_return = tmp;

    if (ret < 0) {
        LOG(ERROR) << "battery tte get failed!";
    } else {
        LOG(WARNING) << "battery tte get succ" << *_aidl_return;
    }
    return ScopedAStatus::ok();
}

// Add battery info method
// This method will return the battery temperature: 20C, 30C
::ndk::ScopedAStatus Battery::getBatteryTemperature(int32_t* _aidl_return) {
    int32_t tmp = 0;
    int32_t ret = readFromFile(Battery::BATTERY_TEMPERATURE, &tmp);
    *_aidl_return = tmp;

    if (ret < 0) {
        LOG(ERROR) << "battery temperature failed!";
    }
    return ScopedAStatus::ok();
}

::ndk::ScopedAStatus Battery::getBatteryCycleCount(int32_t* _aidl_return) {
    int32_t tmp = 0;
    int32_t ret = readFromFile(Battery::BATTERY_CYCLE_COUNT, &tmp);
    *_aidl_return = tmp;

    if (ret < 0) {
        LOG(ERROR) << "battery cycle count failed!";
    }
    return ScopedAStatus::ok();
}

::ndk::ScopedAStatus Battery::getBatteryVoltage(int32_t* _aidl_return) {
    int32_t tmp = 0;
    int32_t ret = readFromFile(Battery::BATTERY_VOLTAGE, &tmp);
    *_aidl_return = tmp;

    if (ret < 0) {
        LOG(ERROR) << "battery voltage failed!";
    }
    return ScopedAStatus::ok();
}

::ndk::ScopedAStatus Battery::getBatteryCurrent(int32_t* _aidl_return) {
    int32_t tmp = 0;
    int32_t ret = readFromFile(Battery::BATTERY_CURRENT, &tmp);
    *_aidl_return = tmp;

    if (ret < 0) {
        LOG(ERROR) << "battery current failed!";
    }

    return ScopedAStatus::ok();
}

::ndk::ScopedAStatus Battery::getBatteryVoltageForNum(int32_t numId, int32_t* _aidl_return) {
    int32_t ret = -1;
    if (numId == 1) {
        ret = readFromFile(Battery::BATTERY_VOLTAGE, _aidl_return);
    } else if (numId == 2) {
        ret = readFromFile(Battery::BATTERY_VOLTAGE_NUM2, _aidl_return);
    }

    if (ret < 0) {
        LOG(ERROR) << "get battery voltage failed! ";
    } else {
        LOG(WARNING) << "Battery voltage is: " << *_aidl_return << ", num = " << numId;
    }
    return ScopedAStatus::ok();
}

::ndk::ScopedAStatus Battery::getBatteryCurrentForNum(int32_t numId, int32_t* _aidl_return) {
    int32_t ret = -1;
    if (numId == 1) {
        ret = readFromFile(Battery::BATTERY_CURRENT, _aidl_return);
    } else if (numId == 2) {
        ret = readFromFile(Battery::BATTERY_CURRENT_NUM2, _aidl_return);
    }

    if (ret < 0) {
        LOG(ERROR) << "Battery current get failed! ";
    } else {
        LOG(WARNING) << "Battery current is: " << *_aidl_return << ", num = " << numId;
    }
    return ScopedAStatus::ok();

}

::ndk::ScopedAStatus Battery::getBatteryTemperatureForNum(int32_t numId, int32_t* _aidl_return) {
    int32_t ret = -1;
    if (numId == 1) {
        ret = readFromFile(Battery::BATTERY_TEMPERATURE, _aidl_return);
    } else if (numId == 2) {
        ret = readFromFile(Battery::BATTERY_TEMPERATURE_NUM2, _aidl_return);
    }

    if (ret < 0) {
        LOG(ERROR) << "Battery temperature get failed! ";
    } else {
        LOG(WARNING) << "Battery temperature is: " << *_aidl_return << ", num = " << numId;
    }
    return ScopedAStatus::ok();

}

// QI stylus pen charging command
::ndk::ScopedAStatus Battery::setStylusQiCommand(int32_t value, bool* _aidl_return) {
    *_aidl_return = writeToFile(Battery::QI_PEN_STYLUE, value);
    if (!*_aidl_return) {
        LOG(ERROR) << "set QI stylus qi command failed!";
    } else {
        LOG(WARNING) << "set QI stylus qi command: " << value;
    }

    return ScopedAStatus::ok();
}

// Add dual battery manufacturer interface
::ndk::ScopedAStatus Battery::getBatteryManufacturerForNum(int32_t numId, std::string* _aidl_return) {
    int32_t ret = -1;
    if (numId == 1) {
        ret = getIntField(Battery::BATTERY_MANUFACTURER);
    } else if (numId == 2) {
        ret = getIntField(Battery::BATTERY_MANUFACTURER_NUM2);
    }

    *_aidl_return = std::to_string(ret);
    if (ret < 0) {
        LOG(ERROR) << "Battery manufacturer get failed! ";
    } else {
        LOG(WARNING) << "Battery manufacturer is: " << *_aidl_return << ", num = " << numId;
    }
    return ScopedAStatus::ok();
}

/* Sycamore code for SYCAMORE-4173 by kangkai4 at 20241129 start */
//Add Bypass setting
::ndk::ScopedAStatus Battery::setBypassLevel(int32_t in_bp_level, bool* _aidl_return) {
    LOG(WARNING) << "set Bypass : " << in_bp_level;
    *_aidl_return = writeToFile(Battery::BYPASS_SETTING, in_bp_level);

    return ScopedAStatus::ok();
}

::ndk::ScopedAStatus Battery::getBypassLevel(int32_t* _aidl_return) {
    int32_t ret = readFromFile(Battery::BYPASS_SETTING, _aidl_return);

    if (ret < 0) {
        LOG(ERROR) << "get Battery Protected num failed! ";
    /*
    } else {
        LOG(WARNING) << "get Battery Protected num: " << *_aidl_return;
    */
    }
    return ScopedAStatus::ok();
}
/* Sycamore code for SYCAMORE-4173 by kangkai4 at 20241129 end */

} // battery
} // hardware
} // lenovo
} // vendor
} // aidl