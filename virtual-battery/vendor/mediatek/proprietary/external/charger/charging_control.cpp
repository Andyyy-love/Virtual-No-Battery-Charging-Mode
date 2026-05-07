/* Copyright Statement:
 *
 * This software/firmware and related documentation ("MediaTek Software") are
 * protected under relevant copyright laws. The information contained herein is
 * confidential and proprietary to MediaTek Inc. and/or its licensors. Without
 * the prior written permission of MediaTek inc. and/or its licensors, any
 * reproduction, modification, use or disclosure of MediaTek Software, and
 * information contained herein, in whole or in part, shall be strictly
 * prohibited.
 * 
 * MediaTek Inc. (C) 2010. All rights reserved.
 * 
 * BY OPENING THIS FILE, RECEIVER HEREBY UNEQUIVOCALLY ACKNOWLEDGES AND AGREES
 * THAT THE SOFTWARE/FIRMWARE AND ITS DOCUMENTATIONS ("MEDIATEK SOFTWARE")
 * RECEIVED FROM MEDIATEK AND/OR ITS REPRESENTATIVES ARE PROVIDED TO RECEIVER
 * ON AN "AS-IS" BASIS ONLY. MEDIATEK EXPRESSLY DISCLAIMS ANY AND ALL
 * WARRANTIES, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE OR
 * NONINFRINGEMENT. NEITHER DOES MEDIATEK PROVIDE ANY WARRANTY WHATSOEVER WITH
 * RESPECT TO THE SOFTWARE OF ANY THIRD PARTY WHICH MAY BE USED BY,
 * INCORPORATED IN, OR SUPPLIED WITH THE MEDIATEK SOFTWARE, AND RECEIVER AGREES
 * TO LOOK ONLY TO SUCH THIRD PARTY FOR ANY WARRANTY CLAIM RELATING THERETO.
 * RECEIVER EXPRESSLY ACKNOWLEDGES THAT IT IS RECEIVER'S SOLE RESPONSIBILITY TO
 * OBTAIN FROM ANY THIRD PARTY ALL PROPER LICENSES CONTAINED IN MEDIATEK
 * SOFTWARE. MEDIATEK SHALL ALSO NOT BE RESPONSIBLE FOR ANY MEDIATEK SOFTWARE
 * RELEASES MADE TO RECEIVER'S SPECIFICATION OR TO CONFORM TO A PARTICULAR
 * STANDARD OR OPEN FORUM. RECEIVER'S SOLE AND EXCLUSIVE REMEDY AND MEDIATEK'S
 * ENTIRE AND CUMULATIVE LIABILITY WITH RESPECT TO THE MEDIATEK SOFTWARE
 * RELEASED HEREUNDER WILL BE, AT MEDIATEK'S OPTION, TO REVISE OR REPLACE THE
 * MEDIATEK SOFTWARE AT ISSUE, OR REFUND ANY SOFTWARE LICENSE FEES OR SERVICE
 * CHARGE PAID BY RECEIVER TO MEDIATEK FOR SUCH MEDIATEK SOFTWARE AT ISSUE.
 *
 * The following software/firmware and/or related documentation ("MediaTek
 * Software") have been modified by MediaTek Inc. All revisions are subject to
 * any receiver's applicable license agreements with MediaTek Inc.
 */

#include "main.h"
#if KPOC_ENABLE_ANIME
#include "bootlogo.h"
#endif
#include "monitor_hang.h"
#include <linux/fb.h>
#include <hardware_legacy/power.h>

#define UEVENT_BATTERY_CHANGE	"power_supply/batt"
//#define BOOTMODE_PATH "/sys/class/BOOT/BOOT/boot/boot_mode"
#define BOOTMODE_PATH "/proc/device-tree/chosen/atag,boot"
#define IHEALTH_DESCRIPTOR "android.hardware.health.IHealth/default"
#define CHARGER_FAST_PATH "/sys/devices/platform/charger/usb_type"

static int bc = 0;
static int firstTime = 0;
static int inDraw = 0;
static int backlight_started = 0;
static int nCurrentState = LIGHTS_STATE_UNKNOWN;
static int nChgAnimDuration_msec = 10000;
static int nCbInterval_msec = 250;
static int backlight_on = 1;
static int oneshot = 1;
static const char *WAKELOCK_ID = "kpoc_charger";
static unsigned int key_trigger_suspend = 0;

int VBAT_POWER_ON = VBAT_POWER_ON_DEFAULT;
int VBAT_POWER_OFF = VBAT_POWER_OFF_DEFAULT;
int lcd_backlight_level = LCD_BL_LEVEL;

pthread_mutex_t mutex, mutexlstate, mutextimer;
pthread_cond_t cond, timer_cond;

static sp<IHealth> g_health;
static std::shared_ptr<IHealth_aidl> g_health_aidl;

struct uevent_handler {
    void (*handler)(void *data, const char *msg, int msg_len);
    void *handler_data;
    LIST_ENTRY(uevent_handler) list;
};

void translateToHidl(const HealthInfo_aidl& in,
                    ::android::hardware::health::V1_0::HealthInfo* out) {
    out->chargerAcOnline = in.chargerAcOnline;
    out->chargerUsbOnline = in.chargerUsbOnline;
    out->chargerWirelessOnline = in.chargerWirelessOnline;
    out->maxChargingCurrent = in.maxChargingCurrentMicroamps;
    out->maxChargingVoltage = in.maxChargingVoltageMicrovolts;
    out->batteryStatus =
            static_cast<::android::hardware::health::V1_0::BatteryStatus>(in.batteryStatus);
    out->batteryHealth =
            static_cast<::android::hardware::health::V1_0::BatteryHealth>(in.batteryHealth);
    out->batteryPresent = in.batteryPresent;
    out->batteryLevel = in.batteryLevel;
    out->batteryVoltage = in.batteryVoltageMillivolts;
    out->batteryTemperature = in.batteryTemperatureTenthsCelsius;
    out->batteryCurrent = in.batteryCurrentMicroamps;
    out->batteryCycleCount = in.batteryCycleCount;
    out->batteryFullCharge = in.batteryFullChargeUah;
    out->batteryChargeCounter = in.batteryChargeCounterUah;
    out->batteryTechnology = in.batteryTechnology;
}

void health_service_init(void)
{
	int ret = 0;
    g_health = get_health_service();
    if (g_health == nullptr) {
        const std::string instance(IHEALTH_DESCRIPTOR);
        ndk::SpAIBinder healthBinder = ndk::SpAIBinder(AServiceManager_getService(instance.c_str()));
        g_health_aidl = IHealth_aidl::fromBinder(healthBinder);
        if (g_health_aidl == nullptr) {
            KPOC_LOGI("Could not retrieve health service\n");
            return;
        }
    }
    ret = __system_property_set((const char *)"debug.mediatek.kpoc.health_service_init", (const char *)"1");
    if (ret < 0) {
        KPOC_LOGI("__system_property_set failed\n");
        return;
    }
}

void start_backlight()
{
    int val_bootmode = readSys_int(BOOTMODE_PATH);
    int vbat_microvolt;
    bool ac_online;
    int ret;
    HealthInfo info = {};

    ret = get_health_info(&info);
    if (ret < 0) {
        KPOC_LOGI("%s: cannot get HealthInfo\n", __func__);
        return;
    }

    ac_online = info.legacy.chargerAcOnline;
    vbat_microvolt = info.legacy.batteryVoltage * 1000;
    KPOC_LOGI("val_bootmode = %d, val_ac = %d\n", val_bootmode, ac_online);

    if ((val_bootmode == 9) && (ac_online == 0))
    {
#ifdef MTK_BATLOWV_NO_PANEL_ON_EARLY
        lcd_backlight_level = 0;
        if (vbat_microvolt >= VBAT_POWER_ON)
        {
            lcd_backlight_level = LCD_LOW_BAT_BL_LEVEL;
        }
#else
        lcd_backlight_level = LCD_LOW_BAT_BL_LEVEL;
#endif
    }

    HwLightState backlight;
    backlight.color = 0xff000000 | (lcd_backlight_level << 16)
                      | (lcd_backlight_level << 8) | lcd_backlight_level;
    backlight.flashMode = FlashMode::NONE;
    backlight.flashOnMs = 0;
    backlight.flashOffMs = 0;
    backlight.brightnessMode = BrightnessMode::USER;
    set_light_brightness(LightType::BACKLIGHT, backlight);
}

void stop_backlight()
{
    HwLightState backlightOff;
    backlightOff.color = 0u;
    backlightOff.flashMode = FlashMode::NONE;
    backlightOff.flashOnMs = 0;
    backlightOff.flashOffMs = 0;
    backlightOff.brightnessMode = BrightnessMode::USER;
    set_light_brightness(LightType::BACKLIGHT, backlightOff);

    backlight_on = 0;
}

static const char *chg_volt_path[] = {
    "/sys/devices/platform/charger/ADC_Charger_Voltage",
    "/sys/devices/platform/battery/ADC_Charger_Voltage",
};

int is_charging_source_available()
{
    static bool pre_usb_online, pre_ac_online;
    bool usb_online, ac_online, wireless_online;
    int i = 0, vchr = 0;
    int ret = 0;
    HealthInfo info = {};

    ret = get_health_info(&info);
    if (ret < 0) {
        KPOC_LOGI("%s: cannot get HealthInfo\n", __func__);
        return 0;
    }

    usb_online = info.legacy.chargerUsbOnline;
    ac_online = info.legacy.chargerAcOnline;
    wireless_online = info.legacy.chargerWirelessOnline;

    if(((pre_usb_online && !usb_online) || (pre_ac_online && !ac_online)) && !inDraw) {
        key_trigger_suspend = 0;
        pthread_cond_signal(&cond);
    }

    pre_usb_online = usb_online;
    pre_ac_online = ac_online;
    /* Find path for charger voltage */
    for (i = 0; i < ARRAY_SIZE(chg_volt_path); i++) {
        vchr = get_int_value(chg_volt_path[i]);
        if (vchr != -1)
            break;
    }

    KPOC_LOGI("in %s(), usb:%d ac:%d wireless:%d vchr:%d\n", __func__,
            usb_online, ac_online, wireless_online, vchr);

    return (usb_online || ac_online || wireless_online || vchr >= 2500);
}

int is_charging_source_status()
{
	int ret = 0;
	HealthInfo info = {};
	::android::hardware::health::V1_0::BatteryStatus battery_status;

    ret = get_health_info(&info);
    if (ret < 0) {
        KPOC_LOGI("%s: cannot get HealthInfo\n", __func__);
        return 0;
    }

	battery_status = info.legacy.batteryStatus;


    KPOC_LOGI("battery_status:%d\n",battery_status);

    return (int)(battery_status);
}

//static unsigned int key_trigger_suspend = 0;
void trigger_anim()
{
	if(inDraw) {
		key_trigger_suspend = 1;
		return;
	}
	if (!is_charging_source_available()) {
		KPOC_LOGI("no charging source, skip drawing anim\n");
		return;
	}
	key_trigger_suspend = 0;
	pthread_cond_signal(&cond);
}

void start_charging_anim(int reason)
{
	KPOC_LOGI("%s: inDraw:%d, reason:%d\n",__FUNCTION__, inDraw, reason);
	trigger_anim();
}

int get_health_info(HealthInfo *info)
{
    HealthInfo_aidl info_aidl = {};

    if (g_health_aidl == nullptr) {
        if (g_health == nullptr) {
            KPOC_LOGI("%s: No health service, cannot get status\n", __func__);
            return -EINVAL;
        }

        auto ret = g_health->getHealthInfo([&](Result r, HealthInfo out) {
            if (r != Result::SUCCESS) {
                KPOC_LOGI("Cannot get HealthInfo, r=%d\n", r);
                return;
            }
            *info = out;
        });
        if (!ret.isOk()) {
            KPOC_LOGI("transaction error\n");
            return -EINVAL;
        }

        return 0;
    }

    auto status = g_health_aidl->getHealthInfo(&info_aidl);
    if (!status.isOk()) {
        KPOC_LOGI("Cannot get HealthInfo\n");
        info = nullptr;
    }
    if (info == nullptr) {
        KPOC_LOGI("transaction error\n");
        return -EINVAL;
    }
    translateToHidl(info_aidl, &info->legacy);

    return 0;
}

static int get_health_capacity(int *capacity)
{
    if (g_health_aidl == nullptr) {
        if (g_health == nullptr) {
            KPOC_LOGI("%s: No health service, cannot get status\n", __func__);
            return -EINVAL;
        }

        auto ret = g_health->getCapacity([&](Result r, int value) {
            if (r != Result::SUCCESS) {
                KPOC_LOGI("Cannot get capacity, r=%d\n", r);
                return;
            }
            *capacity = value;
        });
        if (!ret.isOk()) {
            KPOC_LOGI("transaction error\n");
            return -EINVAL;
        }

        return 0;
    }

    auto ret = g_health_aidl->getCapacity(capacity);
    if (!ret.isOk()) {
        KPOC_LOGI("transaction error\n");
        return -EINVAL;
    }

    return 0;
}

static int get_capacity()
{
    int ret = 0;
    int bat_level = 0;

    do {
        ret = get_health_capacity(&bat_level);
        if (ret < 0) {
            KPOC_LOGI("%s: cannot get capacity\n", __func__);
            return 0;
        }

        if(bat_level == -1)
            usleep(100 * 1000);
        KPOC_LOGI("%s: bat_level: %d\n", __func__, bat_level);
    } while (bat_level == -1);

    return bat_level;
}

int get_voltage()
{
    int vbat_microvolt = 0;
    int ret = 0;
    HealthInfo info = {};

    ret = get_health_info(&info);
    if (ret < 0) {
        KPOC_LOGI("%s: cannot get HealthInfo\n", __func__);
        return 0;
    }

    vbat_microvolt = info.legacy.batteryVoltage * 1000;
    KPOC_LOGI("%s: batt_vol: %d\n", __func__, vbat_microvolt);

    return vbat_microvolt;
}

static void set_light_state(int state)
{
	pthread_mutex_lock(&mutexlstate);
	nCurrentState = state;
	pthread_mutex_unlock(&mutexlstate);
}

static int lights_full()
{
	set_light_state(LIGHTS_STATE_CHGFULL);
	lights_chgfull();
	return 0;
}

//return 1: leave, 0: chgon
static int lights_on()
{
	int leave = false;

	pthread_mutex_lock(&mutexlstate);
	if (nCurrentState != LIGHTS_STATE_CHGON)
		leave = true;
	pthread_mutex_unlock(&mutexlstate);

	if (!leave) {
		lights_chgon();
		return 0;
	}
	return 1;
}

static int lights_exit()
{
	set_light_state(LIGHTS_STATE_EXIT);
	lights_chgexit();
	return 0;
}

static int uevent_next_event_timeout(char* buffer, int buffer_length, int timeout)
{
	LIST_HEAD(uevent_handler_head, uevent_handler) uevent_handler_list = {};
	pthread_mutex_t uevent_handler_list_lock = PTHREAD_MUTEX_INITIALIZER;
	int fd = -1;

	fd = uevent_get_fd();

	while (1) {
		struct pollfd fds;
		int nr;

		fds.fd = fd;
		fds.events = POLLIN;
		fds.revents = 0;
		nr = poll(&fds, 1, timeout);

		if(nr > 0 && (fds.revents & POLLIN)) {
			int count = recv(fd, buffer, buffer_length, 0);
			if (count > 0) {
				struct uevent_handler *h;
				pthread_mutex_lock(&uevent_handler_list_lock);
				LIST_FOREACH(h, &uevent_handler_list, list)
					h->handler(h->handler_data, buffer, buffer_length);
				pthread_mutex_unlock(&uevent_handler_list_lock);

				return count;
			}
		} else if (nr == 0) {
			KPOC_LOGI("Timed out, no uevent for a long time: %d\n", nr);
			return nr;
		}
}

// won't get here
return 0;
}

void plug_out_animation(int bc, int total_time_msec, int interval_msec, bool once)
{
	struct timeval start;
	int resume_started = 0,backlight_started = 0,cnt = 0;
	int fd_fb, err = 0;
	char filename[32] = {0};

	gettimeofday(&start, NULL);
	bc = get_capacity();
	KPOC_LOGI("key_trigger_suspend = %d: time_exceed(start, total_time_msec) = %d\n", key_trigger_suspend,time_exceed(start, total_time_msec));

	while(!time_exceed(start, total_time_msec) && !key_trigger_suspend) //key_trigger
	{
#if 0
		if (is_charging_source_available())
		{
			return;
		}
		if (!resume_started) {
			resume_started = 1;
			int is_drm_support = is_drm();
			KPOC_LOGI("[charging is_drm: %d\n", is_drm_support);
			if(is_drm_support != 1) {
				/* make fb unblank */
				err = snprintf(filename, sizeof(filename), "/dev/graphics/fb0");
				if (err < 0) {
					KPOC_LOGI("Failed at snprintf: %s\n", strerror(errno));
					return;
				}
				fd_fb = open(filename, O_RDWR);
				if (fd_fb < 0) {
					KPOC_LOGI("Failed to open fb0 device: %s", strerror(errno));
				}
				err = ioctl(fd_fb, FBIOBLANK, FB_BLANK_UNBLANK);
				if (err < 0) {
					KPOC_LOGI("Failed to unblank fb0 device: %s", strerror(errno));
				}
				if (fd_fb >= 0)
					close(fd_fb);
			}
		}
		bc = get_capacity();
#endif
		show_plug_out_capacity(bc);

		if (!backlight_started)
		{
			backlight_started = 1;
			usleep(1000);
			start_backlight();
		}
		usleep(interval_msec*1000);

		if (once == true)
		{
			while(!time_exceed(start, total_time_msec) && !key_trigger_suspend);
			break;
		}
	}
}
static int on_uevent(const char *buf, __attribute__((unused))int len_buf)
{
	int LightState = LIGHTS_STATE_UNKNOWN;
#ifdef VERBOSE_OUTPUT
	KPOC_LOGI("on_uevent, %s\n", buf);
#endif
	if (!strcasestr(buf, UEVENT_BATTERY_CHANGE))
		return 1;

	//if ac or usb online
	if (is_charging_source_available())
	{
		bc = get_capacity();

		if (bc >= 90) {
			lights_full();
		} else {
			pthread_mutex_lock(&mutexlstate);
			LightState = nCurrentState;
			pthread_mutex_unlock(&mutexlstate);
			if (LightState != LIGHTS_STATE_CHGON)
				set_light_state(LIGHTS_CHGON);
			lights_on();
		}
	}
	else {
		//exit_charger(EXIT_CHARGING_MODE);
		KPOC_LOGI("charging source notavailable\n");
	}

	return 1;
}

static void* uevent_thread_routine(__attribute__((unused))void *arg)
{
	char buf[1024];
	int len;

	if (!uevent_init())
	{
		KPOC_LOGI("uevent_init failed.\n");
		return 0;
	}

	while (1)
	{
		len = uevent_next_event_timeout(buf, sizeof(buf) - 1, 60 * 1000);
		if (len > 0) {
			// tick hang detect for incoming uevent
			hang_detect_tick();

			if (!on_uevent(buf, len))
				break;
		} else {
			// tick hang detect for uevent timeout
			hang_detect_tick();
		}
	}
	pthread_exit(NULL);
	return NULL;
}

static void __attribute__((unused)) exit_charing_thread()
{
	inDraw = 0;
	pthread_exit(NULL);
}

// total_time : ms
// interval : ms
#if KPOC_ENABLE_ANIME
static void draw_with_interval(void (*func)(int, int), int bc, int total_time_msec, int interval_msec)
{
	struct timeval start;
	int resume_started = 0, backlight_started = 0, cnt = 0;
	int fd_fb, err = 0;
	char filename[32] = {0};
	gettimeofday(&start, NULL);

	while(!time_exceed(start, total_time_msec) && !key_trigger_suspend && !inExiting)
	{
        // check if need to draw animation before performing drawing
		if (!resume_started) {
			resume_started = 1;
			int is_drm_support = is_drm();
			KPOC_LOGI("[charging is_drm: %d\n", is_drm_support);
			if(is_drm_support != 1) {
				/* make fb unblank */
				err = snprintf(filename, sizeof(filename), "/dev/graphics/fb0");
				if (err < 0) {
					KPOC_LOGI("Failed at snprintf: %s\n", strerror(errno));
					return;
				}
				fd_fb = open(filename, O_RDWR);
				if (fd_fb < 0) {
					KPOC_LOGI("Failed to open fb0 device: %s", strerror(errno));
				}
				err = ioctl(fd_fb, FBIOBLANK, FB_BLANK_UNBLANK);
				if (err < 0) {
					KPOC_LOGI("Failed to unblank fb0 device: %s", strerror(errno));
				}
				if (fd_fb >= 0)
					close(fd_fb);
			}
		}


		if (!is_charging_source_available() && !get_float_charging_state()) {
			KPOC_LOGI("lh !is_charging_source_available() draw_with_interval");
			plug_out_animation(bc,3000,nCbInterval_msec,true);
			usleep(3000 * 1000);
			if (!is_charging_source_available())
				exit_charger(EXIT_CHARGING_MODE);
		} else { 
			func(bc, ++cnt);
		}
		if (!backlight_started) {
			backlight_started = 1;
			usleep(1000);
			start_backlight();
		}
		KPOC_LOGI("draw_with_interval... key_trigger_suspend = %d\n",key_trigger_suspend);
		usleep(interval_msec*1000);
	}
}
#endif

static int wait_until(int (*func)(void), int total_time_msec, int interval_msec){
	struct timeval start;
	gettimeofday(&start, NULL);

    while(!time_exceed(start, total_time_msec)){
        if(func()){
            return 1;
        }
		usleep(interval_msec*1000);
    }
    return 0;
}

#define charging_source_waiting_duration_ms 3000
#define charging_source_waiting_interval_ms 200

#if KPOC_ENABLE_ANIME
static void* draw_thread_routine(__attribute__((unused))void *arg)
{
	int bc;
	int fd_fb, err =0;
    int ret = 0;
	char filename[32] = {0};

	do {
		KPOC_LOGI("draw thread working2...\n");
        // move here to avoid suspend when syncing with surfaceflinger

        if(firstTime){
            // make sure charging source online when in KPOC mode
            // add 2s tolerance
            if(wait_until(is_charging_source_available,
                        charging_source_waiting_duration_ms,
                        charging_source_waiting_interval_ms))
            {
                KPOC_LOGI("wait until charging source available\n");
            }else{
                KPOC_LOGI("charging source not available for %d ms at KPOC starup\n",
                        charging_source_waiting_duration_ms);
            }
            firstTime = 0;
        }
		acquire_wake_lock(PARTIAL_WAKE_LOCK, WAKELOCK_ID);

		inDraw = 1;

		// check the bc offest value
		bc = get_capacity();
		draw_with_interval(bootlogo_show_charging, bc, nChgAnimDuration_msec, nCbInterval_msec);
		stop_backlight();

		// @@@ draw fb again to refresh ddp
		bootlogo_show_charging(bc, 1);

		if (oneshot) {
			if(get_charging_type(CHARGER_FAST_PATH) == -1) {
				ret = __system_property_set((const char *)"debug.mediatek.kpoc.charger_kerenl_drvier_init", (const char *)"0");
                if (ret < 0) {
                    KPOC_LOGI("__system_property_set failed\n");
                    break;
                }
            } else {
				ret = __system_property_set((const char *)"debug.mediatek.kpoc.charger_kerenl_drvier_init", (const char *)"1");
                if (ret < 0) {
                    KPOC_LOGI("__system_property_set failed\n");
                    break;
                }
            }
			oneshot = 0;
		}

		int is_drm_support = is_drm();
		KPOC_LOGI("[charging is_drm: %d\n", is_drm_support);
		if(is_drm_support != 1) {
			/* make fb blank */
			err = snprintf(filename, sizeof(filename), "/dev/graphics/fb0");
			if (err < 0) {
				KPOC_LOGI("Failed at snprintf: %s\n", strerror(errno));
				break;
			}
			fd_fb = open(filename, O_RDWR);
			if (fd_fb < 0) {
				KPOC_LOGI("Failed to open fb0 device: %s", strerror(errno));
				break;
			}
			err = ioctl(fd_fb, FBIOBLANK, FB_BLANK_POWERDOWN);
			if (err < 0) {
				KPOC_LOGI("Failed to blank fb0 device: %s", strerror(errno));
			}
			if (fd_fb >= 0)
				close(fd_fb);
		} else {
			turn_off_drm_display();
		}
		release_wake_lock(WAKELOCK_ID);
		inDraw = 0;

		if(get_float_charging_state())
			pthread_cond_signal(&timer_cond);
        pthread_mutex_lock(&mutex);
		pthread_cond_wait(&cond, &mutex);
		pthread_mutex_unlock(&mutex);
	} while(1);
	pthread_exit(NULL);
	return NULL;
}
#endif

static void* timer_thread_routine(__attribute__((unused))void *arg)
{
	do {
	screen_off_check:
		struct timeval start;

		pthread_mutex_lock(&mutextimer);
		pthread_cond_wait(&timer_cond, &mutextimer);
		pthread_mutex_unlock(&mutextimer);

		gettimeofday(&start, NULL);
		while (!time_exceed(start, 60 * 1000)){
			if (inDraw){
				goto screen_off_check;
			}
		}
		if (get_float_charging_state() && !inDraw) {
			hang_detect_tick();
			start_charging_anim(TRIGGER_ANIM_KEY);
		}
	} while (1);
}
void charging_control()
{
	int ret = 0;
	pthread_attr_t attr, attrd, attrl;
	pthread_t uevent_thread, draw_thread, timer_thread;

	//charging led control
	if (!is_charging_source_available()) {
		lights_exit();
	}

	pthread_mutex_init(&mutexlstate, NULL);

	pthread_mutex_init(&mutex, NULL);
	pthread_mutex_init(&mutextimer, NULL);
	pthread_cond_init(&cond, NULL);
	pthread_cond_init(&timer_cond, NULL);

	pthread_attr_init(&attr);
	pthread_attr_init(&attrd);
	pthread_attr_init(&attrl);

	inDraw = 0;

	ret = pthread_create(&uevent_thread, &attr, uevent_thread_routine, NULL);
	if (ret != 0)
	{
		KPOC_LOGI("create uevt pthread failed.\n");
		exit_charger(EXIT_ERROR_SHUTDOWN);
	}

	firstTime = 1;
#if KPOC_ENABLE_ANIME
	ret = pthread_create(&draw_thread, &attrd, draw_thread_routine, NULL);
	if (ret != 0)
	{
		KPOC_LOGI("create draw pthread failed.\n");
		exit_charger(EXIT_ERROR_SHUTDOWN);
	}
#endif
	ret = pthread_create(&timer_thread, &attrl, timer_thread_routine, NULL);
	if (ret != 0)
	{
		KPOC_LOGI("create timer_pthread failed.\n");
		exit_charger(EXIT_ERROR_SHUTDOWN);
	}
}