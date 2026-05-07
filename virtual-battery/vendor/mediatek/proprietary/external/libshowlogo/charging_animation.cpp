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

#define MTK_LOG_ENABLE 1
#include <dlfcn.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sched.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/mman.h>
#include <time.h>

#include <utils/RefBase.h>
#include <utils/StrongPointer.h>

#include <cutils/ashmem.h>
#include <cutils/log.h>
#include <cutils/atomic.h>
#include <cutils/properties.h>
#include <cutils/klog.h>

/* include linux framebuffer header */
#include <linux/fb.h>
#include <unistd.h>
#include <binder/IBinder.h>
#include <gui/IGraphicBufferProducer.h>

/* include surface flinger header */
#include <gui/LayerState.h>

#include <binder/IPCThreadState.h>

#include <hardware/hardware.h>
#include <hardware/gralloc.h>

#include <android/native_window.h>
#include <android/rect.h>

//#include <ui/Rect.h>
//#include <ui/Region.h>
//#include <ui/DisplayConfig.h>
#include <ui/DisplayState.h>
#include <ui/GraphicBuffer.h>

#include <gui/Surface.h>
#include <gui/SurfaceComposerClient.h>
#include <gui/ISurfaceComposer.h>

#include <fs_mgr.h>

#include <chrono>

/*  include drm_resources */
#include <libdrm_macros.h>

#include "charging_animation.h"
#include "vitural_battery_cn_kernel_new.h"
#include "vitural_battery_en_kernel_new.h"
#include "vitural_battery_new.h"

//#include <sysenv_utils.h>

#include "utils.h"

#ifdef SLOGD
#undef SLOGD
//#define SLOGD(x,...) do { KLOG_ERROR("libshowlogo", x); } while (0)
#define SLOGD(...)                              \
    do                                          \
    {                                           \
        KLOG_ERROR("libshowlogo", __VA_ARGS__); \
    } while (0)
#endif

#ifdef SLOGE
#undef SLOGE
//#define SLOGE(x,...) do { KLOG_ERROR("libshowlogo", x); } while (0)
#define SLOGE(...)                              \
    do                                          \
    {                                           \
        KLOG_ERROR("libshowlogo", __VA_ARGS__); \
    } while (0)
#endif

using namespace android;
using android::fs_mgr::Fstab;
using android::fs_mgr::GetEntryForMountPoint;
using android::fs_mgr::ReadDefaultFstab;
using namespace std::chrono;

#if 0
#if defined(MSSI_MTK_CARRIEREXPRESS_PACK)
#define ENV_MAGIC 'e'
#define ENV_READ _IOW(ENV_MAGIC, 1, int)
#define ENV_WRITE _IOW(ENV_MAGIC, 2, int)
#define BUF_MAX_LEN 20
#define NAME_E "mtk_usp_operator"
#define VALUE_E "120"

/*
struct env_ioctl
{
    char *name;
    int name_len;
    char *value;
    int value_len;
};*/

/* Operator number */
#define OPTR_NUM_OP01 1
#define OPTR_NUM_OP02 2
#define OPTR_NUM_OP09 9

/* Count of logos for different charging animations */
#define LOGOS_COUNT_NORMAL_CHARGING_ANIM 38
#define LOGOS_COUNT_FAST_CHARGING_ANIM 18
#define LOGOS_COUNT_WIRELESS_CHARGING_ANIM 29

#define MAX_OPERATOR_SUPPORT 5

#endif
#endif

sp<SurfaceComposerClient> client;
sp<SurfaceControl> surfaceControl;

ANativeWindow_Buffer outBuffer;
ANativeWindowBuffer *buf;
sp<GraphicBuffer> gb;

sp<Surface> surface;
ui::DisplayState mDisplayState;
//DisplayConfig          dinfo;

static LCM_SCREEN_T phical_screen;

static int fb_fd = 0;
static int drm_fd = 0;
// logo.bin
static uint64_t *logo_addr = NULL;
static unsigned long logo_offset = 0;
// use for decompress logo resource
static void *dec_logo_addr = NULL;
// use for nmap framebuffer
static unsigned int *fb_addr = NULL;
// use double fb address
static unsigned int *lk_fb_addr = NULL;
static unsigned int *charging_fb_addr = NULL;
static unsigned int *kernel_fb_addr = NULL;
static unsigned int use_double_addr = 0;

static struct fb_var_screeninfo vinfo;
static struct fb_fix_screeninfo finfo;
libDrmApi libdrmApi;
chargingAnimationDimensionAPI chargingAnimationDimension;
static unsigned int fb_size = 0;

//dualdisplay - parameters for 2nd LCD - start
//extern & declare in header file
static LCM_SCREEN_T phical_screen_ext;
static unsigned int *fb_addr_ext = NULL;
// use double fb address
static unsigned int *lk_fb_addr_ext = NULL;
static unsigned int *charging_fb_addr_ext = NULL;
static unsigned int *kernel_fb_addr_ext = NULL;
//single or multibin
bool is_multibin = false;
static struct fb_var_screeninfo vinfo_ext;
static struct fb_fix_screeninfo finfo_ext;
libDrmApi libdrmApi_ext;
chargingAnimationDimensionAPI chargingAnimationDimension_ext;
static unsigned int fb_size_ext = 0;
ANativeWindow_Buffer outBuffer_ext;
ANativeWindowBuffer *buf_ext;
sp<Surface> surface_ext;
//dualdisplay - parameters for 2nd LCD - end

static int show_animationm_ver = VERION_NEW_ANIMATION;
static int draw_anim_mode = DRAW_ANIM_MODE_SURFACE;

// kernel logo index may be different for different logo.bin
static int kernel_logo_position = KERNEL_LOGO_INDEX;
// add a flag for exiting on abnormal case
static int error_flag = 0;

//is first time to check float type
static int check_float_first = 1;

// get system properties
int getValue(char *key, char *defValue);

//get the right partition path of logo
#define LOGO_MNT_POINT "/logo"
struct fstab *fstab;

#if 0
#if defined(MSSI_MTK_CARRIEREXPRESS_PACK)
/*
 * get active operator
 *
 */
int get_active_optr(){
    int fd;
    int n;
    int active_optr = -1;

    //struct env_ioctl en_ctl;
    char *name = NULL;
    const char *value = NULL;

    //memset(&en_ctl,0x00,sizeof(struct env_ioctl));

    fd= open("/proc/lk_env",O_RDWR);

    if (fd<= 0) {
        if (MTK_LOG_ENABLE == 1) {
            SLOGD("[libshowlogo] ERROR open fail %d\n",fd);
        }
        return -1;
    }
    if (MTK_LOG_ENABLE == 1) {
        SLOGD("[libshowlogo] open /proc/lk_env ok\n");
    }
    name = (char *)malloc(BUF_MAX_LEN);
    memset(name,0x00,BUF_MAX_LEN);
    memcpy(name,NAME_E,strlen(NAME_E)+1);

#if 0 //test
    value = (char *)malloc(BUF_MAX_LEN);
    memset(value,0x00,BUF_MAX_LEN);
    memcpy(value,VALUE_E,strlen(VALUE_E)+1);

    en_ctl.name = name;
    en_ctl.value = value;
    en_ctl.name_len = strlen(name)+1;
    en_ctl.value_len = strlen(value)+1;
    SLOGD("[libshowlogo] write %s = %s\n",name,value);
    n=ioctl(fd,ENV_WRITE,&en_ctl);
    if (n<0) {
        SLOGD("[libshowlogo] ERROR write fail %d\n",n);
    } else {
        SLOGD("[libshowlogo] SysEnv Write 3 successfull %d\n",n);
    }
    memset(value,0x00,BUF_MAX_LEN);
    en_ctl.value_len = BUF_MAX_LEN;
#endif

    printf("read %s \n",name);

    //n=ioctl(fd,ENV_READ,&en_ctl);
    value = sysenv_get((const char *)name);

    if (value == NULL) {
        if (MTK_LOG_ENABLE == 1) {
            SLOGD("[libshowlogo] ERROR read fail \n");
        }

        //SLOGD("[libshowlogo] ERROR read fail errorno: %d\n",errno);
        //SLOGD("[libshowlogo] ERROR read fail error_msg: %s\n",strerror(errno));
    } else {
        //active_optr = atoi(en_ctl.value);
        active_optr = atoi(value);
        if (MTK_LOG_ENABLE == 1) {
            SLOGD("[libshowlogo] SysEnv read value: %d\n",active_optr);
        }
    }
    if (MTK_LOG_ENABLE == 1) {
        SLOGD("[libshowlogo] read %s : %s\n",name,value);
    }
    free(name);
    //free(value);
    close(fd);
    return active_optr;
}

/*
 * get default operator.
 *
 */
#ifdef GLOBAL_DEVICE_DEFAULT_OPTR
static int get_default_operator(){
#if GLOBAL_DEVICE_DEFAULT_OPTR == OPTR_NUM_OP01
    return OPTR_NUM_OP01;
#elif GLOBAL_DEVICE_DEFAULT_OPTR == OPTR_NUM_OP02
    return OPTR_NUM_OP02;
#elif GLOBAL_DEVICE_DEFAULT_OPTR == OPTR_NUM_OP09
    return OPTR_NUM_OP09;
#else
    return 0;
#endif
}
#endif

/*
 * get system properties from device.mk file
 */
int getValue(char* key, char* defValue) {
    char buf[PROPERTY_VALUE_MAX];
    int len  = property_get(key,buf,defValue);
    if (MTK_LOG_ENABLE == 1){
        SLOGD("[libshowlogo: %s %d] buf = %s, key = %s\n",__FUNCTION__,__LINE__, buf, key);
    }
    return (atoi(buf));
}

/*
 * get Logo index when the global device feature is enabled.
 *
 */
static int get_logo_index(bool isBoot)
{
    int logo_index = 0;
    int optrs[MAX_OPERATOR_SUPPORT];
    int i = 0, j = 0, optr_index = -1, optr_number = -1;
    int to_include_fast_charging = getValue("ro.vendor.mtk_fast_charging_support" , "0");

#if defined(MTK_CARRIEREXPRESS_PACK_OP01)
    optrs[i++] = OPTR_NUM_OP01;
#endif

#if defined(MTK_CARRIEREXPRESS_PACK_OP02)
    optrs[i++] = OPTR_NUM_OP02;
#endif

#if defined(MTK_CARRIEREXPRESS_PACK_OP09)
    optrs[i++] = OPTR_NUM_OP09;
#endif

    logo_index = 1;
#if !defined(MTK_ALPS_BOX_SUPPORT)
    logo_index += LOGOS_COUNT_NORMAL_CHARGING_ANIM;
#endif
if( to_include_fast_charging == 1) {
    logo_index += LOGOS_COUNT_FAST_CHARGING_ANIM;
}
#if defined(MSSI_MTK_WIRELESS_CHARGER_SUPPORT)
    logo_index += LOGOS_COUNT_WIRELESS_CHARGING_ANIM;
#endif

    // sysenv read property.
    optr_number = get_active_optr();
    if (optr_number == -1) {
#if !defined(GLOBAL_DEVICE_DEFAULT_OPTR)
        if (MTK_LOG_ENABLE == 1) {
            SLOGD("[libshowlogo: get_logo_index() - Show OM]\n");
        }
        if (isBoot) {
            return 0;
        } else {
            return kernel_logo_position;
        }
#else
        if (MTK_LOG_ENABLE == 1) {
            SLOGD("[libshowlogo: get_logo_index() - Default Operator]\n");
        }
        optr_number = get_default_operator();
#endif
    } else {
        if (MTK_LOG_ENABLE == 1) {
            SLOGD("[libshowlogo: get_logo_index() - Active Optr Number : %d]\n",optr_number);
        }
    }

    for(j = 0; j < i && j < MAX_OPERATOR_SUPPORT; j++){
            if (optrs[j] == optr_number) {
                if (MTK_LOG_ENABLE == 1) {
                    SLOGD("[libshowlogo: get_logo_index() - Active Optr Index : %d]\n",j);
                }
        optr_index = j;
        break;
        }
    }

    if (optr_index == -1) {
        if (MTK_LOG_ENABLE == 1) {
            SLOGD("[libshowlogo: get_logo_index() - Operator not present in region : %d]\n",optr_number);
        }
        if(isBoot) {  //Show OM
            return 0;
        } else {
            return kernel_logo_position;
        }
    }
    if (MTK_LOG_ENABLE == 1) {
        SLOGD("[libshowlogo: get_logo_index() - Logo index without Optr : %d]\n",logo_index);
    }
    logo_index = logo_index + optr_index*2;  // calculate the LK Logo index#endif
    if (MTK_LOG_ENABLE == 1) {
        SLOGD("[libshowlogo: get_logo_index() - Logo index with Optr : %d]\n",logo_index);
    }
    if (!isBoot) {
        logo_index += 1;
    }

    return logo_index;
}
#endif

#endif
/*
 * free fstab
 *
 */
void free_fstab(void)
{
}

/*
 * Set charging animation version
 *
 */
void set_anim_version(int version)
{
    if (MTK_LOG_ENABLE == 1)
    {
        SLOGD("[libshowlogo: %s %d]set animationm_version =  :%d, MAX_SUPPORT_ANIM_VER = %d", __FUNCTION__, __LINE__, version, MAX_SUPPORT_ANIM_VER);
    }
    if ((version > (MAX_SUPPORT_ANIM_VER)) || (version < VERION_OLD_ANIMATION))
    {
        show_animationm_ver = VERION_NEW_ANIMATION;
    }
    else
    {
        show_animationm_ver = VERION_NEW_ANIMATION;
    }
    if (MTK_LOG_ENABLE == 1)
    {
        SLOGD("[libshowlogo: %s %d]show_animationm_ver =  :%d", __FUNCTION__, __LINE__, show_animationm_ver);
    }
}

/*
 * Set charging animation drawing method
 *
 */
void set_draw_mode(int draw_mode)
{
    if (MTK_LOG_ENABLE == 1)
    {
        SLOGD("[libshowlogo: %s %d]0 --use framebuffer, 1--use surface flinger ,draw_anim_mode =  :%d", __FUNCTION__, __LINE__, draw_mode);
    }
    if ((draw_mode != (DRAW_ANIM_MODE_FB)) && (draw_mode != (DRAW_ANIM_MODE_SURFACE)))
    {
        draw_anim_mode = DRAW_ANIM_MODE_SURFACE;
    }
    else
    {
        draw_anim_mode = draw_mode;
    }
}

/*   return value:
 *         0, error or read nothing
 *        !0, read counts
 */
int read_from_file(const char *path, char *buf, int size)
{
    if (!path)
    {
        return 0;
    }

    int fd = open(path, O_RDONLY);
    if (fd == -1)
    {
        return 0;
    }

    int count = read(fd, buf, size);
    if (count > 0)
    {
        count = (count < size) ? count : size - 1;
        while (count > 0 && buf[count - 1] == '\n')
            count--;
        buf[count] = '\0';
    }
    else
    {
        buf[0] = '\0';
    }

    close(fd);
    return count;
}

int get_charging_type(const char* path) {
	int size = 32 , num = 0;;
    char buf[size];
    if (!read_from_file(path, buf, size)) {
		SLOGI("get path:%s fail\n", path);
        return 0;
	}
	num = atoi(buf);

	switch(num) {
		case 0:
			SLOGD("get_charging_type: Unknown\n");
			return 0;
		case 1:
		case 2:
		case 3:
		case 4:
			SLOGD("get_charging_type: normal charging\n");
			return 1;
		case 5:
		case 6:
		case 7:
		case 8:
			SLOGD("get_charging_type: fast charging\n");
			return 2;
		case 9:
			SLOGD("get_charging_type: float\n");
			return 3;
		default:
			return 0;
	}
}

int get_int_value(const char *path)
{
    int size = 32;
    char buf[size];
    if (!read_from_file(path, buf, size)) {
		SLOGI("get path:%s fail\n", path);
        return 0;
	}
    return atoi(buf);
}

/*
 * return value:
 *     1: fast charging
 *     0: normal charging
 */
int get_fast_charging_state()
{
    int state = get_charging_type(CHARGER_FAST_PATH);
    int charge_online = get_int_value(CHARGER_ONLINE_PATH);
	if(state == 2 && charge_online == 1) {
	    if (MTK_LOG_ENABLE == 1)
	    {
	        SLOGI("get_fast_charging_state: %d\n", state);
	    }

	    return state;
	} else {
		SLOGD("get_fast_charging_state is fail\n");
		return 0;
	}

}

int get_charge_online_state()
{
    int charge_online = get_int_value(CHARGER_ONLINE_PATH);
	if(charge_online == 1) {
		SLOGD("get_charge_online_state: %d %d \n",charge_online, __LINE__);
		return charge_online;
	} else {
		SLOGD("get_charge_online_state: %d %d \n",charge_online, __LINE__);
		return 0;
	}
}

/*
 * return value:
 *     true: charge to 20% soc
 *     false: charge full
 */
bool get_bypass_setting()
{
    int bypass_setting = get_int_value(BYPASS_SETTING);
     if (MTK_LOG_ENABLE == 1)
    {
        SLOGD("get_bypass_setting: %d %d \n",bypass_setting, __LINE__);
    }
    if (bypass_setting / 100000) {
        return  true;
    } else {
        return false;
    }
}

bool get_float_charging_state()
{
    bool is_float_flag = false;
    int float_type_flag = get_charging_type(FLOAT_TYPE_FLAG_PATH);
	if(float_type_flag == 3) {
	    SLOGD("get_float_charging_state: %d %d \n",float_type_flag, __LINE__);
		is_float_flag = true;
	    return is_float_flag;
	} else {
		SLOGD("get_float_charging_state is fail\n");
		return is_float_flag;
	}
}

bool get_battery_online_status()
{
    bool is_online = false;
    int present = get_int_value(BATTERY_PRESENT_PATH);
    if (present > 0)
    {
        is_online = true;
    }

    if (MTK_LOG_ENABLE == 1)
    {
        SLOGE("get_battery_online_status: %d, present:%d\n", is_online, present);
    }

    return is_online;
}

/*
 * return temperature:
 *
 *
 */
int get_battery_temperature()
{
    int temperature = get_int_value(BATTERY_TEMPERATURE_PATH);
    if (MTK_LOG_ENABLE == 1)
    {
        SLOGE("get_battery_temperature: %d\n", temperature);
    }

    return temperature;
}
/*
 * Charging animation init
 *
 */
void anim_init()
{
    chargingAnimationDimension.IS_DRM = 0;
    fb_fd = open(FB_NODE_PATH, O_RDWR);
    if (fb_fd < 0)
    {
        SLOGE("[libshowlogo: %s %d]open dev file fail, errno = %d  trying DRM \n", __FUNCTION__, __LINE__, errno);
        close(fb_fd);
        int res = anim_drm_pre_init();
        if(res != 0) {
            SLOGE("[libshowlogo: %s %d] failed to preinit drm, charging animation will not be shown \n", __FUNCTION__, __LINE__, errno);
            chargingAnimationDimension.IS_DRM = -1;
            chargingAnimationDimension_ext.IS_DRM = -1;
            error_flag = 1;
            return;
        }
    }
    else
    {
        SLOGE("[libshowlogo: %s %d] FB_SUPPORTED with fb_fd = %d \n", __FUNCTION__, __LINE__, fb_fd);

        ioctl(fb_fd, FBIOGET_VSCREENINFO, &vinfo);
        ioctl(fb_fd, FBIOGET_FSCREENINFO, &finfo);
        SLOGE("[libshowlogo: %s %d] IOCTL SUCCESS with fb_fd = %d \n", __FUNCTION__, __LINE__, fb_fd);
        chargingAnimationDimension.HDISPLAY = vinfo.xres;
        chargingAnimationDimension.VDISPLAY = vinfo.yres;
        chargingAnimationDimension.FB_BPP = vinfo.bits_per_pixel;
        chargingAnimationDimension.FB_WIDTH = vinfo.xres_virtual;
        chargingAnimationDimension.IS_DRM = 0;
        SLOGD("[libshowlogo: %s %d] LCD 1 - FB_SUPPORTED HDISPLAY= %d VDISPLAY= %d FB_BPP= %d FB_WIDTH= %d \n", __FUNCTION__, __LINE__, chargingAnimationDimension.HDISPLAY, chargingAnimationDimension.VDISPLAY, chargingAnimationDimension.FB_BPP, chargingAnimationDimension.FB_WIDTH);
        
        ioctl(fb_fd, FBIOGET_VSCREENINFO, &vinfo_ext);
        ioctl(fb_fd, FBIOGET_FSCREENINFO, &finfo_ext);
        SLOGE("[libshowlogo: %s %d] IOCTL SUCCESS with fb_fd = %d \n", __FUNCTION__, __LINE__, fb_fd);
        chargingAnimationDimension_ext.HDISPLAY = vinfo_ext.xres;
        chargingAnimationDimension_ext.VDISPLAY = vinfo_ext.yres;
        chargingAnimationDimension_ext.FB_BPP = vinfo_ext.bits_per_pixel;
        chargingAnimationDimension_ext.FB_WIDTH = vinfo_ext.xres_virtual;
        chargingAnimationDimension_ext.IS_DRM = 0;
        SLOGD("[libshowlogo: %s %d] LCD 2 - FB_SUPPORTED HDISPLAY= %d VDISPLAY= %d FB_BPP= %d FB_WIDTH= %d \n", __FUNCTION__, __LINE__, chargingAnimationDimension_ext.HDISPLAY, chargingAnimationDimension_ext.VDISPLAY, chargingAnimationDimension_ext.FB_BPP, chargingAnimationDimension_ext.FB_WIDTH);
    }

    init_charging_animation_ui_dimension();
    if (MTK_LOG_ENABLE == 1)
    {
        SLOGD("[libshowlogo: %s %d]\n", __FUNCTION__, __LINE__);
    }
    anim_logo_init();

    if (chargingAnimationDimension.IS_DRM == 1)
    {
        SLOGD("[libshowlogo: %s %d]DRM init in progress \n", __FUNCTION__, __LINE__);
        anim_drm_init();
    }
    else
    {
        SLOGD("[libshowlogo: %s %d] FB init in progress \n", __FUNCTION__, __LINE__);
        if (draw_anim_mode == (DRAW_ANIM_MODE_FB))
        {
            anim_fb_init();
        }
        else
        {
            anim_surface_init();
        }
    }
}

/*
 * Charging animation set buffer address
 *
 */

void anim_set_buffer_address(int index)
{
    if (index == BOOT_LOGO_INDEX)
    {
        fb_addr = lk_fb_addr;

        if (is_ext_lcd_present()) //2nd lcd present
            fb_addr_ext = lk_fb_addr_ext;
    }
    else if (index == kernel_logo_position)
    {
        fb_addr = kernel_fb_addr;

        if (is_ext_lcd_present()) //2nd lcd present
            fb_addr_ext = kernel_fb_addr_ext;
    }
    else
    {
        anim_fb_addr_switch();
    }
}

/*
 * Charging animation deinit
 *
 */
void anim_deinit()
{
    if (MTK_LOG_ENABLE == 1)
    {
        SLOGD("[libshowlogo: %s %d]\n", __FUNCTION__, __LINE__);
    }
    anim_logo_deinit();

    if (chargingAnimationDimension.IS_DRM == 1)
    {
        anim_drm_deinit();
    }
    else
    {
        if (draw_anim_mode == (DRAW_ANIM_MODE_FB))
        {
            anim_fb_deinit();
        }
        else
        {
            anim_surface_deinit();
        }
    }
}

/*
 *
 * To calculate logo offset for logo_2 in case of multibin
 * fd - recieves fd for logo.img
 * return offset value for logo_2
 *
 */
static unsigned long calculate_logo2_offset (int fd)
{
    int len = 0;
    unsigned long offset = 0;
    part_hdr_t para;
    SLOGD("[libshowlogo: %s %d] \n", __FUNCTION__, __LINE__);
    for(int i = 0; i < 3; i++) {
        // skip logo 1
        len = read(fd,&para,sizeof(part_hdr_t));

        SLOGD("[libshowlogo: %s %d] looping in logo data, loop= %d len= %d \n", __FUNCTION__, __LINE__, i, len);

        if(strcmp(para.info.name, "logo") == 0)
        {
            set_logo_multibin_status(false);
            lseek(fd, 0, SEEK_SET);
            SLOGD("[libshowlogo: %s %d] Singlebin logo\n", __FUNCTION__, __LINE__);
            return offset;
        }

        offset += sizeof(union part_hdr_t) + ROUNDUP(para.info.dsz, para.info.align_sz);
        off_t off_seek = lseek(fd,offset,0);
        if (off_seek == -1) {
            SLOGD("[libshowlogo: %s %d] - lseek failed \n",__FUNCTION__, __LINE__);
            offset = 0;
            return offset;
        }
    }
    SLOGD("[libshowlogo: %s %d] Multibin logo\n", __FUNCTION__, __LINE__);
    SLOGD("[libshowlogo: %s %d] return offset = %lu \n", __FUNCTION__, __LINE__, offset);
    set_logo_multibin_status(true);
    return offset;
}

/*
 * Charging animation logo.bin related init
 *
 */
void anim_logo_init(void)
{
    // read and de-compress logo data here
    int fd = 0;
    int len = 0;
    SLOGD("[libshowlogo: %s %d]",__FUNCTION__, __LINE__);

    fd = open("/dev/block/by-name/logo", O_RDONLY);

    if (fd > 0){
        SLOGD("[libshowlogo: %s %d] - /logo as active slot \n", __FUNCTION__, __LINE__);
    }
    else
    {
        fd = open("/dev/block/by-name/logo_a", O_RDONLY);
        SLOGD("[libshowlogo: %s %d] - /logo_a as active logo slot \n", __FUNCTION__, __LINE__);
    }

    // for calulating offset in case of multi-bin logo
    logo_offset = calculate_logo2_offset(fd);

    //single bin
    if(!get_logo_multibin_status())
    {
        // if single-bin is found, use fd
        logo_addr = (uint64_t *)malloc(LOGO_BUFFER_SIZE);

        if (logo_addr == NULL)
        {
            if (MTK_LOG_ENABLE == 1)
            {
                //single bin
                SLOGE("[libshowlogo: %s %d]allocate logo buffer fail \n", __FUNCTION__, __LINE__);
            }
            goto error_return;
        }
        // skip image header
        len = read(fd, logo_addr, 512);
        if (len < 0)
        {
            if (MTK_LOG_ENABLE == 1)
            {
                SLOGE("[libshowlogo: %s %d]read from logo addr for buffer is failed! \n", __FUNCTION__, __LINE__);
            }
            goto error_return;
        }

        // read the img data
        len = read(fd, logo_addr, LOGO_BUFFER_SIZE - 512);
    }
    else
    { // multibin
        part_hdr_t para;

        // skip img header
        len = read(fd,&para,sizeof(part_hdr_t));
        SLOGD("\n ========== \n magic = %x \n dsize = %x \n name = %s offset = %lu len = %d \n", para.info.magic , para.info.dsz, para.info.name, logo_offset, len);
        logo_addr = (uint64_t *)malloc(para.info.dsz);

        if (logo_addr == NULL)
        {
            if (MTK_LOG_ENABLE == 1)
            {
                SLOGE("[libshowlogo: %s %d]allocate logo buffer fail, size=0x%08x \n", __FUNCTION__, __LINE__, LOGO_BUFFER_SIZE);
            }
            goto error_return;
        }
        if (para.info.dsz <= sizeof(part_hdr_t)) {
            SLOGE("[libshowlogo: %s %d]para.info.dsz is wrong, dsize = %x \n", __FUNCTION__, __LINE__, para.info.dsz);
            goto error_return;
        }
        // (1) read the image
        len = read(fd, logo_addr, para.info.dsz);
    }

    if (len < 0)
    {
        if (MTK_LOG_ENABLE == 1)
        {
            SLOGE("[libshowlogo: %s %d]read from logo addr for buffer is failed! \n", __FUNCTION__, __LINE__);
        }
        goto error_return;
    }
    close(fd);

    if (show_animationm_ver > 0)
    {
        unsigned int *pinfo = (unsigned int *)logo_addr;
        if (MTK_LOG_ENABLE == 1)
        {
            SLOGD("[libshowlogo: %s %d]pinfo[0]=%d, pinfo[1]=%d\n", __FUNCTION__, __LINE__, pinfo[0], pinfo[1]);
        }

        if ((show_animationm_ver == VERION_WIRELESS_CHARGING_ANIMATION) && (pinfo[0] < ANIM_V2_LOGO_NUM))
        {
            set_anim_version(VERION_NEW_ANIMATION);
        }
        if (pinfo[0] < ANIM_V1_LOGO_NUM)
        {
            kernel_logo_position = ANIM_V0_LOGO_NUM - 1;
            set_anim_version(VERION_OLD_ANIMATION);
        }
    }
    if (MTK_LOG_ENABLE == 1)
    {
        SLOGD("[libshowlogo: %s %d]show_animationm_ver =  :%d", __FUNCTION__, __LINE__, show_animationm_ver);
    }
    SLOGD("[libshowlogo: %s %d]",__FUNCTION__, __LINE__);
    return;

error_return:
    if (fd >= 0)
    {
        close(fd);
    }
    sleep(3);
    error_flag = 1;
    SLOGD("[libshowlogo: %s %d] error return !!!\n", __FUNCTION__, __LINE__);
    // to prevent interlace operation with MD reset
}

/*
 * Charging animation logo.bin related deinit
 *
 */
void anim_logo_deinit(void)
{
    if (MTK_LOG_ENABLE == 1)
    {
        SLOGD("[libshowlogo: %s %d]\n", __FUNCTION__, __LINE__);
    }
    free_fstab();
    free(logo_addr);
    logo_addr = NULL;
    free(dec_logo_addr);
    dec_logo_addr = NULL;
}

/*
 * Charging animation framebuffer related init
 *
 */
int anim_fb_init(void)
{
    if (MTK_LOG_ENABLE == 1)
    {
        SLOGD("[libshowlogo: %s %d]\n", __FUNCTION__, __LINE__);
    }
    fb_size = finfo.line_length * vinfo.yres;
    dec_logo_addr = (uint64_t *)malloc(fb_size);

    if (dec_logo_addr == NULL)
    {
        if (MTK_LOG_ENABLE == 1)
        {
            SLOGE("[libshowlogo: %s %d]allocate dec_logo_addr buffer fail \n", __FUNCTION__, __LINE__);
        }
        assert(0);
    }

    lk_fb_addr = (unsigned int *)mmap(0, fb_size * 3, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, 0);
    charging_fb_addr = (unsigned int *)((uint64_t) lk_fb_addr + fb_size);
    kernel_fb_addr = (unsigned int *)((uint64_t) charging_fb_addr + fb_size);
    fb_addr = lk_fb_addr;

    if (MTK_LOG_ENABLE == 1)
    {
        SLOGD("[libshowlogo: %s %d]vinfo:xres  = %d, yres = %d, xres_virtual =%d, bits_per_pixel = %d,red.offset = %d,blue.offset = %d\n", __FUNCTION__, __LINE__, vinfo.xres, vinfo.yres, vinfo.xres_virtual, vinfo.bits_per_pixel, vinfo.red.offset, vinfo.blue.offset);
        SLOGD("[libshowlogo: %s %d]fb_size =%d, fb_addr = %ld,charging_fb_addr=%ld\n", __FUNCTION__, __LINE__, fb_size, (long)fb_addr, (long)charging_fb_addr);
    }

    if (fb_addr == NULL || charging_fb_addr == NULL)
    {
        if (MTK_LOG_ENABLE == 1)
            SLOGE("ChargingAnimation mmap for 1st LCD failed\n");

        munmap(lk_fb_addr, fb_size * 2);
        close(fb_fd);
        error_flag = 1;
        return -1;
    }

    phical_screen.bits_per_pixel = chargingAnimationDimension.FB_BPP;
    phical_screen.fill_dst_bits = chargingAnimationDimension.FB_BPP;
    phical_screen.red_offset = vinfo.red.offset;
    phical_screen.blue_offset = vinfo.blue.offset;
    phical_screen.width = chargingAnimationDimension.HDISPLAY;
    phical_screen.height = chargingAnimationDimension.VDISPLAY;
    phical_screen.allignWidth = fb_size / (((chargingAnimationDimension.FB_BPP / 8) * chargingAnimationDimension.VDISPLAY));
    phical_screen.needAllign = 1;
    phical_screen.need180Adjust = 1;
    phical_screen.fb_size = fb_size;

    if (MTK_LOG_ENABLE == 1)
        SLOGD("[libshowlogo: %s %d]MTK_LCM_PHYSICAL_ROTATION = %s\n", __FUNCTION__, __LINE__, MTK_LCM_PHYSICAL_ROTATION);

    int rotation = getRotation();
    if (MTK_LOG_ENABLE == 1)
        SLOGD("[libshowlogo: %s %d]rotation = %d\n", __FUNCTION__, __LINE__, rotation);

    if (ORIENTATION_270 == rotation) //270
        phical_screen.rotation = 270;
    else if (ORIENTATION_90 == rotation) //90
        phical_screen.rotation = 90;
    else if ((ORIENTATION_180 == rotation) && (phical_screen.need180Adjust == 1)) //180
        phical_screen.rotation = 180;
    else
        phical_screen.rotation = 0;

    if (MTK_LOG_ENABLE == 1)
    {
        SLOGD("[libshowlogo]phical_screen: width= %d,height= %d,bits_per_pixel =%d,needAllign = %d,allignWidth=%d rotation =%d ,need180Adjust = %d\n",
              phical_screen.width, phical_screen.height,
              phical_screen.bits_per_pixel, phical_screen.needAllign,
              phical_screen.allignWidth, phical_screen.rotation, phical_screen.need180Adjust);
        SLOGD("[libshowlogo: %s %d]show old animtion= 1, running show_animationm_ver %d\n", __FUNCTION__, __LINE__, show_animationm_ver);
        SLOGD("[libshowlogo: %s %d]draw_anim_mode = 1, running mode %d\n", __FUNCTION__, __LINE__, draw_anim_mode);
    }

    //dualdisplay - start
    if (is_ext_lcd_present()) //2nd lcd present
    {
        phical_screen_ext.bits_per_pixel = chargingAnimationDimension_ext.FB_BPP;
        phical_screen_ext.fill_dst_bits = chargingAnimationDimension_ext.FB_BPP;
        phical_screen_ext.red_offset = vinfo_ext.red.offset;
        phical_screen_ext.blue_offset = vinfo_ext.blue.offset;
        phical_screen_ext.width = chargingAnimationDimension_ext.HDISPLAY;
        phical_screen_ext.height = chargingAnimationDimension_ext.VDISPLAY;
        phical_screen_ext.allignWidth = fb_size_ext / (((chargingAnimationDimension_ext.FB_BPP / 8) * chargingAnimationDimension_ext.VDISPLAY));
        phical_screen_ext.needAllign = 1;
        phical_screen_ext.need180Adjust = 1;
        phical_screen_ext.fb_size = fb_size_ext;
        //dualdisplay - 2nd LCD not to support rotation
        phical_screen_ext.rotation = 0;

        fb_size_ext = finfo_ext.line_length * vinfo_ext.yres;
        lk_fb_addr_ext = (unsigned int *)mmap(0, fb_size_ext * 3, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, 0);
        charging_fb_addr_ext = (unsigned int *)((uint64_t) lk_fb_addr_ext + fb_size_ext);
        kernel_fb_addr_ext = (unsigned int *)((uint64_t) charging_fb_addr_ext + fb_size_ext);
        fb_addr_ext = lk_fb_addr_ext;

        if (MTK_LOG_ENABLE == 1)
        {
            SLOGD("[libshowlogo: %s %d]vinfo_ext:xres  = %d, yres = %d, xres_virtual =%d, bits_per_pixel = %d,red.offset = %d,blue.offset = %d\n", __FUNCTION__, __LINE__, vinfo_ext.xres, vinfo_ext.yres, vinfo_ext.xres_virtual, vinfo_ext.bits_per_pixel, vinfo_ext.red.offset, vinfo_ext.blue.offset);
            SLOGD("[libshowlogo: %s %d]fb_size_ext =%d, fb_addr_ext = %ld,charging_fb_addr_ext=%ld\n", __FUNCTION__, __LINE__, fb_size_ext, (long)fb_addr_ext, (long)charging_fb_addr_ext);
        }
        if (fb_addr_ext == NULL || charging_fb_addr_ext == NULL)
        {
            if (MTK_LOG_ENABLE == 1)
            {
                SLOGE("ChargingAnimation mmap for 2nd LCD failed\n");
            }
            munmap(lk_fb_addr, fb_size * 2);
            munmap(lk_fb_addr_ext, fb_size_ext * 2);
            close(fb_fd);
            error_flag = 1;

            return -1;
        }
        if (MTK_LOG_ENABLE == 1)
        {
            //dualdisplay
            SLOGD("[libshowlogo]phical_screen_ext: width= %d,height= %d,bits_per_pixel =%d,needAllign = %d,allignWidth=%d rotation =%d ,need180Adjust = %d\n",
                  phical_screen_ext.width, phical_screen_ext.height,
                  phical_screen_ext.bits_per_pixel, phical_screen_ext.needAllign,
                  phical_screen_ext.allignWidth, phical_screen_ext.rotation, phical_screen_ext.need180Adjust);
        }
    }
    //dualdisplay - end

    return 0;
}

/*
 * Charging animation framebuffer related deinit
 *
 */
void anim_fb_deinit(void)
{
    if (MTK_LOG_ENABLE == 1)
        SLOGD("[libshowlogo: %s %d]\n", __FUNCTION__, __LINE__);

    munmap(lk_fb_addr, fb_size * 3);
    if (is_ext_lcd_present()) //2nd lcd present
        munmap(lk_fb_addr_ext, fb_size_ext * 3);

    close(fb_fd);
}

/*
 * Charging animation framebuffer switch buffer
 *
 */
void anim_fb_addr_switch(void)
{
    if (MTK_LOG_ENABLE == 1)
    {
        SLOGD("[libshowlogo: %s %d]use_double_addr =%d \n", __FUNCTION__, __LINE__, use_double_addr);
    }
    if (use_double_addr == 0)
    {
        use_double_addr++;
        fb_addr = kernel_fb_addr;

        if (is_ext_lcd_present()) //2nd lcd present
            fb_addr_ext = kernel_fb_addr_ext;
    }
    else
    {
        use_double_addr = 0;
        fb_addr = charging_fb_addr;

        if (is_ext_lcd_present()) //2nd lcd present
            fb_addr_ext = charging_fb_addr_ext;
    }
    if (MTK_LOG_ENABLE == 1)
    {
        SLOGD("[libshowlogo: %s %d]LCD 1 - fb_addr =%ld \n", __FUNCTION__, __LINE__, (long)fb_addr);

        if (is_ext_lcd_present()) //2nd lcd present
          SLOGD("[libshowlogo: %s %d]LCD 2 - fb_addr_ext =%ld \n", __FUNCTION__, __LINE__, (long)fb_addr_ext);
    }
}

/*
 * Charging animation framebuffer update
 *
 */
void anim_fb_disp_update(void)
{
    if (MTK_LOG_ENABLE == 1)
    {
        SLOGD("[libshowlogo: %s %d]\n", __FUNCTION__, __LINE__);
    }

    if (fb_addr == charging_fb_addr)
    {
        vinfo.yoffset = vinfo.yres;
    }
    else if (fb_addr == kernel_fb_addr)
    {
        vinfo.yoffset = vinfo.yres * 2;
    }
    else
    {
        vinfo.yoffset = 0;
    }

    if (is_ext_lcd_present()) //2nd lcd present
    {
        if (fb_addr_ext == charging_fb_addr_ext)
        {
            vinfo_ext.yoffset = vinfo_ext.yres;
        }
        else if (fb_addr_ext == kernel_fb_addr_ext)
        {
            vinfo_ext.yoffset = vinfo_ext.yres * 2;
        }
        else
        {
            vinfo_ext.yoffset = 0;
        }
    }
    vinfo.activate |= (FB_ACTIVATE_FORCE | FB_ACTIVATE_NOW);
    vinfo_ext.activate |= (FB_ACTIVATE_FORCE | FB_ACTIVATE_NOW);

    if (ioctl(fb_fd, FBIOPUT_VSCREENINFO, &vinfo) < 0)
    {
        if (MTK_LOG_ENABLE == 1)
        {
            SLOGE("ioctl FBIOPUT_VSCREENINFO for LCD 1 - flip failed\n");
        }
    }

    if ((is_ext_lcd_present()) && (ioctl(fb_fd, FBIOPUT_VSCREENINFO, &vinfo_ext) < 0))
    {
        if (MTK_LOG_ENABLE == 1)
        {
            SLOGE("ioctl FBIOPUT_VSCREENINFO for LCD 2 - flip failed\n");
        }
    }
}

/*
 * Charging animation surface flinger related init
 *
 */
void anim_surface_init(void)
{
#if 0
    SurfaceComposerClient::Transaction t;
    if (MTK_LOG_ENABLE == 1)
    {
        SLOGD("[libshowlogo: %s %d]\n", __FUNCTION__, __LINE__);
    }
    client = new SurfaceComposerClient();

    const sp<IBinder> dtoken = SurfaceComposerClient::getInternalDisplayToken();

    if (MTK_LOG_ENABLE == 1)
    {
        SLOGD("[libshowlogo: %s %d]ChargingAnimation getDisplayInfo()...\n", __FUNCTION__, __LINE__);
    }
    status_t status = SurfaceComposerClient::getActiveDisplayConfig(dtoken, &dinfo);
    SurfaceComposerClient::getDisplayState(dtoken, &mDisplayState);

    if (status)
    {
        if (MTK_LOG_ENABLE == 1)
        {
            SLOGD("[libshowlogo: %s %d]error=%x %d", __FUNCTION__, __LINE__, status, status);
        }
    }
    if (MTK_LOG_ENABLE == 1)
    {
        // FIXME: orientation need to get from DisplayInfo
        SLOGD("[libshowlogo: %s %d]dinfo.w=%d,dinfo.h=%d\n", __FUNCTION__, __LINE__, dinfo.resolution.getWidth(), dinfo.resolution.getHeight());
        SLOGD("[libshowlogo: %s %d]set default orientation\n", __FUNCTION__, __LINE__);
    }

    t.setDisplayProjection(dtoken, mDisplayState.orientation, Rect(dinfo.resolution.getWidth(), dinfo.resolution.getHeight()), Rect(dinfo.resolution.getWidth(), dinfo.resolution.getHeight()));
    t.apply();

    int dinfo_width = dinfo.resolution.getWidth();
    int dinfo_height = dinfo.resolution.getHeight();
    if (MTK_LOG_ENABLE == 1)
    {
        SLOGD("[libshowlogo: %s %d]dinfo_width=%d, dinfo_height=%d \n", __FUNCTION__, __LINE__, dinfo_width, dinfo_height);
    }

    surfaceControl = client->createSurface(String8("charging-surface"), dinfo_width, dinfo_height, PIXEL_FORMAT_BGRA_8888);
    if (MTK_LOG_ENABLE == 1)
    {
        SLOGD("[libshowlogo: %s %d]set layer geometry\n", __FUNCTION__, __LINE__);
    }
    // set layer geometry
    t.setLayer(surfaceControl, 2000000)
        .apply();

    // data structure to access surface content
    surface = surfaceControl->getSurface();

    phical_screen.width = dinfo.resolution.getWidth();
    phical_screen.height = dinfo.resolution.getHeight();
    // for we adjust the roration avove, so no need to consider rotation
    phical_screen.rotation = 0;
    phical_screen.need180Adjust = 0;

    int err;
    int fenceFd = -1;
    ANativeWindow *window = surface.get();
    status_t result = native_window_api_connect(window, NATIVE_WINDOW_API_CPU);
    if (result == NO_ERROR)
    {
        err = window->dequeueBuffer(window, &buf, &fenceFd);
        sp<Fence> mFence(new Fence(fenceFd));
        mFence->wait(Fence::TIMEOUT_NEVER);
        if (err)
        {
            if (MTK_LOG_ENABLE == 1)
            {
                SLOGE("[libshowlogo: %s %d]%s\n", __FUNCTION__, __LINE__, strerror(-err));
            }
        }
        gb = GraphicBuffer::from(buf);
        if (MTK_LOG_ENABLE == 1)
        {
            SLOGD("[libshowlogo: %s %d]gb->getStride(): %d\n", __FUNCTION__, __LINE__, gb->getStride());
        }
        /* phical_screen: needAllign and  need180Adjust need confirm */
        phical_screen.needAllign = 1;
        phical_screen.allignWidth = gb->getStride();

        window->cancelBuffer(window, buf, fenceFd);

        // use PIXEL_FORMAT_RGBA_8888 for surface flinger
        phical_screen.bits_per_pixel = 32;
        phical_screen.fill_dst_bits = 32;
        fb_size = dinfo.resolution.getWidth() * dinfo.resolution.getHeight() * 4;

        dec_logo_addr = malloc(fb_size);
        if (MTK_LOG_ENABLE == 1)
        {
            SLOGD("[libshowlogo: %s %d]fb_size =%d\n", __FUNCTION__, __LINE__, fb_size);
        }

        phical_screen.fb_size = fb_size;
        if (MTK_LOG_ENABLE == 1)
        {
            SLOGD("[show_logo_common] phical_screen: width= %d,height= %d,bits_per_pixel =%d,needAllign = %d,allignWidth=%d rotation =%d ,need180Adjust = %d\n",
                  phical_screen.width, phical_screen.height, phical_screen.bits_per_pixel, phical_screen.needAllign, phical_screen.allignWidth, phical_screen.rotation, phical_screen.need180Adjust);
        }
    }
#endif
}

/*
 * Charging animation surface flinger related deinit
 *
 */
void anim_surface_deinit(void)
{
    if (MTK_LOG_ENABLE == 1)
    {
        SLOGD("[libshowlogo: %s %d]\n", __FUNCTION__, __LINE__);
    }
    surface = surfaceControl->getSurface();
    ANativeWindow *window = surface.get();
    //dualdisplay - TBD
    native_window_api_disconnect(window, NATIVE_WINDOW_API_CPU);
    //surfaceControl->release(); // TODO : Find alternate method to release surface. Done for build error
    client->dispose();
}

/*
 * Show special logo with the index in logo.bin
 *
 */
void anim_show_logo(int index)
{
    if (MTK_LOG_ENABLE == 1)
    {
        SLOGD("[libshowlogo: %s %d]draw_anim_mode=%d, show  index =  %d\n", __FUNCTION__, __LINE__, draw_anim_mode, index);
    }

    if (draw_anim_mode == (DRAW_ANIM_MODE_FB))
    {
        anim_set_buffer_address(index);
        fill_animation_logo(index, fb_addr, dec_logo_addr, logo_addr, phical_screen);

        if (is_ext_lcd_present()) //2nd lcd present
            fill_animation_logo(index, fb_addr_ext, dec_logo_addr, logo_addr, phical_screen_ext);

        if (chargingAnimationDimension.IS_DRM == 1)
            anim_drm_disp_update();
        else
            anim_fb_disp_update();
    }
    else
    {
        ARect tmpRect, tmpRect_ext;

        tmpRect.left = 0;
        tmpRect.top = 0;
        tmpRect.right = phical_screen.width;
        tmpRect.bottom = phical_screen.height;

        tmpRect_ext.left = 0;
        tmpRect_ext.top = 0;
        tmpRect_ext.right = phical_screen_ext.width;
        tmpRect_ext.bottom = phical_screen_ext.height;

        status_t lockResult = surface->lock(&outBuffer, &tmpRect);
        status_t lockResult_ext = surface_ext->lock(&outBuffer_ext, &tmpRect_ext);

        if (MTK_LOG_ENABLE == 1)
        {
            SLOGD("[libshowlogo: %s %d]outBuffer.bits = %ld\n", __FUNCTION__, __LINE__, (long)outBuffer.bits);
            SLOGD("[libshowlogo: %s %d]surface->lock return =  0x%08x,  %d\n", __FUNCTION__, __LINE__, lockResult, lockResult);

            SLOGD("[libshowlogo: %s %d]outBuffer_ext.bits = %ld\n", __FUNCTION__, __LINE__, (long)outBuffer_ext.bits);
            SLOGD("[libshowlogo: %s %d]ext surface->lock return =  0x%08x,  %d\n", __FUNCTION__, __LINE__, lockResult_ext, lockResult_ext);
        }
        if (0 == lockResult)
        {
            fill_animation_logo(index, (void *)outBuffer.bits, dec_logo_addr, logo_addr, phical_screen);
            fill_animation_logo(index, (void *)outBuffer_ext.bits, dec_logo_addr, logo_addr, phical_screen_ext);
            surface->unlockAndPost();
            surface_ext->unlockAndPost();
        }
    }
}

/*
 * Show first boot logo when phone boot up
 *
 */
void show_boot_logo(void)
{
    if (MTK_LOG_ENABLE == 1)
    {
        SLOGD("[libshowlogo: %s %d]show boot logo, index = 0 \n", __FUNCTION__, __LINE__);
    }
    if (error_flag == 0)
    {

        anim_show_logo(BOOT_LOGO_INDEX);
    }
}

/*
 * Show kernel logo when phone boot up
 *
 */
void show_kernel_logo()
{
    if (MTK_LOG_ENABLE == 1)
    {
        SLOGD("[libshowlogo: %s %d]show kernel logo, index = 38 \n", __FUNCTION__, __LINE__);
    }
    if (error_flag == 0)
    {
        anim_show_logo(kernel_logo_position);
    }
}

/*
 * Show low battery logo
 *
 */
void show_low_battery(void)
{
    if (MTK_LOG_ENABLE == 1)
    {
        SLOGD("[libshowlogo: %s %d]show low battery logo, index = 2 \n", __FUNCTION__, __LINE__);
    }
    if (error_flag == 0)
    {
        anim_show_logo(LOW_BATTERY_INDEX);
    }
}

/*
 * Show charging over logo
 *
 */
void show_charger_ov_logo(void)
{
    if (MTK_LOG_ENABLE == 1)
    {
        SLOGD("[libshowlogo: %s %d]show charger_ov logo, index = 3 \n", __FUNCTION__, __LINE__);
    }
    if (error_flag == 0)
    {
        anim_show_logo(CHARGER_OV_INDEX);
    }
}

/*
 * Show battery temperature low logo
 *
 */
void show_battery_temperature_low(void)
{
    if (MTK_LOG_ENABLE == 1)
    {
        SLOGD("[libshowlogo: %s %d]show battery temperature low logo, index = xx \n",__FUNCTION__,__LINE__);
    }
    if (error_flag == 0)
    {
        anim_show_logo(BAT_TEMP_LOW_INDEX);
    }
}

/*
 * Show battery temperature high logo
 *
 */
void show_battery_temperature_high(void)
{
    if (MTK_LOG_ENABLE == 1)
    {
        SLOGD("[libshowlogo: %s %d]show battery temperature high logo, index = xx \n",__FUNCTION__,__LINE__);
    }
    if (error_flag == 0)
    {
        anim_show_logo(BAT_TEMP_HIGH_INDEX);
    }
}

/*
 * Show charging no battery logo
 *
 */
void show_charging_no_battery(void)
{
    if (MTK_LOG_ENABLE == 1)
    {
        SLOGD("[libshowlogo: %s %d]show charging no battery logo, index = xx \n",__FUNCTION__,__LINE__);
    }
    if (error_flag == 0)
    {
        anim_show_logo(CHARGE_NO_BATTERY_INDEX);
    }
}

/*
 * Show charger float logo
 *
 */
void show_charger_bad_contact(void)
{
    if (MTK_LOG_ENABLE == 1)
    {
        SLOGD("[libshowlogo: %s %d]show charger float logo, index = xx \n",__FUNCTION__,__LINE__);
    }
    if (error_flag == 0)
    {
        anim_show_logo(CHARGER_FLOAT_INDEX_1);
    }
}
/*
 * Draw black screen
 *
 */
void show_black_logo(void)
{
    if (MTK_LOG_ENABLE == 1)
        SLOGD("[libshowlogo: %s %d]\n", __FUNCTION__, __LINE__);

    if (draw_anim_mode == (DRAW_ANIM_MODE_FB))
    {
        anim_fb_addr_switch();
        memset((unsigned short *)fb_addr, 0x00, fb_size);

        if (is_ext_lcd_present()) //2nd lcd present
            memset((unsigned short *)fb_addr_ext, 0x00, fb_size_ext);

        if (chargingAnimationDimension.IS_DRM == 1)
            anim_drm_disp_update();
        else
            anim_fb_disp_update();
    }
    else
    {
        ARect tmpRect, tmpRect_ext;

        tmpRect.left = 0;
        tmpRect.top = 0;
        tmpRect.right = phical_screen.width;
        tmpRect.bottom = phical_screen.height;

        tmpRect_ext.left = 0;
        tmpRect_ext.top = 0;
        tmpRect_ext.right = phical_screen_ext.width;
        tmpRect_ext.bottom = phical_screen_ext.height;

        status_t lockResult = surface->lock(&outBuffer, &tmpRect);
        status_t lockResult_ext = surface_ext->lock(&outBuffer_ext, &tmpRect_ext);

        if (MTK_LOG_ENABLE == 1)
        {
            SLOGD("[libshowlogo: %s %d]outBuffer.bits = %ld, surface->lock return =  0x%08x,\n", __FUNCTION__, __LINE__, (long)outBuffer.bits, lockResult);
            SLOGD("[libshowlogo: %s %d]outBuffer_ext.bits = %ld, surface->lock return =  0x%08x,\n", __FUNCTION__, __LINE__, (long)outBuffer_ext.bits, lockResult);
        }
        if (0 == lockResult)
        {
            ssize_t bpr = outBuffer.stride * bytesPerPixel(outBuffer.format);
            ssize_t bpr_ext = outBuffer_ext.stride * bytesPerPixel(outBuffer_ext.format);
            if (MTK_LOG_ENABLE == 1)
            {
                SLOGD("[libshowlogo: %s %d]bpr = %d\n", __FUNCTION__, __LINE__, (int)bpr);
                SLOGD("[libshowlogo: %s %d]bpr_ext = %d\n", __FUNCTION__, __LINE__, (int)bpr_ext);
            }
            memset((unsigned short *)outBuffer.bits, 0x00, bpr * outBuffer.height);
            memset((unsigned short *)outBuffer_ext.bits, 0x00, bpr_ext * outBuffer_ext.height);

            surface->unlockAndPost();
            surface_ext->unlockAndPost();
        }
    }
}


/*
 * Show plug out with battery capacity
 *
 */
void show_plug_out_capacity(unsigned int capacity)
{
    show_animationm_ver = VERION_PLUGOUT_CHARGING_ANIMATION;
    bool ext_lcd_supported = false;
    if (MTK_LOG_ENABLE == 1)
    {
        SLOGD("[libshowlogo: %s %d]capacity =%d\n", __FUNCTION__, __LINE__, capacity);
    }
    if(chargingAnimationDimension.IS_DRM == -1) {
        SLOGE("[libshowlogo: %s %d]DRM was failed. no animation will be there \n", __FUNCTION__, __LINE__);
        return;
    }
    ext_lcd_supported = is_ext_lcd_present();
    int has_fast_charging = getValue("ro.vendor.mtk_fast_charging_support", "0");
    if (draw_anim_mode == (DRAW_ANIM_MODE_FB))
    {
        anim_fb_addr_switch();
        if (has_fast_charging == 1)
        {
            if (2 != show_animationm_ver && get_fast_charging_state())
            {
                fill_animation_battery_by_ver(capacity, (void *)fb_addr, dec_logo_addr, logo_addr, phical_screen, show_animationm_ver);

                if (ext_lcd_supported) //2nd lcd present
                    fill_animation_battery_by_ver(capacity, (void *)fb_addr_ext, dec_logo_addr, logo_addr, phical_screen_ext, draw_anim_mode);
            }
            else
            {
                fill_animation_battery_by_ver(capacity, (void *)fb_addr, dec_logo_addr, logo_addr, phical_screen, show_animationm_ver);

                if (ext_lcd_supported) //2nd lcd present
                    fill_animation_battery_by_ver(capacity, (void *)fb_addr_ext, dec_logo_addr, logo_addr, phical_screen_ext, draw_anim_mode);
            }
        }
        else
        {
            fill_animation_battery_by_ver(capacity, (void *)fb_addr, dec_logo_addr, logo_addr, phical_screen, show_animationm_ver);

            if (ext_lcd_supported) //2nd lcd present
                fill_animation_battery_by_ver(capacity, (void *)fb_addr_ext, dec_logo_addr, logo_addr, phical_screen_ext, draw_anim_mode);
        }

        if (chargingAnimationDimension.IS_DRM == 1)
            anim_drm_disp_update();
        else
            anim_fb_disp_update();
    }
    else
    {
        ARect tmpRect, tmpRect_ext;

        tmpRect.left = 0;
        tmpRect.top = 0;
        tmpRect.right = phical_screen.width;
        tmpRect.bottom = phical_screen.height;

        tmpRect_ext.left = 0;
        tmpRect_ext.top = 0;
        tmpRect_ext.right = phical_screen.width;
        tmpRect_ext.bottom = phical_screen.height;

        status_t lockResult = surface->lock(&outBuffer, &tmpRect);
        status_t lockResult_ext = surface_ext->lock(&outBuffer_ext, &tmpRect_ext);
        if (MTK_LOG_ENABLE == 1)
        {
            SLOGD("[libshowlogo: %s %d]outBuffer.bits = %ld, surface->lock return =  0x%08x,\n", __FUNCTION__, __LINE__, (long)outBuffer.bits, lockResult);
            SLOGD("[libshowlogo: %s %d]outBuffer_ext.bits = %ld, surface->lock return =  0x%08x,\n", __FUNCTION__, __LINE__, (long)outBuffer_ext.bits, lockResult_ext);
        }
        if (0 == lockResult)
        {
            if (has_fast_charging == 1)
            {
                if (2 != show_animationm_ver && get_fast_charging_state())
                {
                    fill_animation_battery_fast_charging(capacity, (void *)outBuffer.bits, dec_logo_addr, logo_addr, phical_screen, draw_anim_mode);
                    fill_animation_battery_fast_charging(capacity, (void *)outBuffer_ext.bits, dec_logo_addr, logo_addr, phical_screen_ext, draw_anim_mode);
                }
                else
                {
                    fill_animation_battery_by_ver(capacity, (void *)outBuffer.bits, dec_logo_addr, logo_addr, phical_screen, show_animationm_ver);
                    fill_animation_battery_by_ver(capacity, (void *)outBuffer_ext.bits, dec_logo_addr, logo_addr, phical_screen_ext, show_animationm_ver);
                }
            }
            else
            {
                fill_animation_battery_by_ver(capacity, (void *)outBuffer.bits, dec_logo_addr, logo_addr, phical_screen, show_animationm_ver);
                fill_animation_battery_by_ver(capacity, (void *)outBuffer_ext.bits, dec_logo_addr, logo_addr, phical_screen_ext, show_animationm_ver);
            }
            surface->unlockAndPost();
            surface_ext->unlockAndPost();
        }
    }
}

bool time_exceed(struct timeval start, int duration_msec)
{
    struct timeval now;
    gettimeofday(&now, NULL);

    if((now.tv_sec - start.tv_sec)*1000000 + now.tv_usec - start.tv_usec > duration_msec*1000)
        return true;
    else
        return false;
}

static int index = 0;
void show_float_photo(void)
{
    if (check_float_first)
    {
        struct timeval start;
        gettimeofday(&start, NULL);
        while (!time_exceed(start, 500))
        {
            anim_show_logo(ANIM_V1_BACKGROUND_INDEX);
            break;
        }
        check_float_first = 0;
    }
    if (draw_anim_mode == (DRAW_ANIM_MODE_FB))
    {
        anim_fb_addr_switch();
        anim_show_logo(CHARGER_FLOAT_INDEX_1 + index % 2);
        index++;
    }
    anim_fb_addr_switch();
    anim_show_logo(CHARGER_FLOAT_INDEX_1 + index % 2);
}

/*
 * Show charging animation with battery capacity
 *
 */
void show_battery_capacity(unsigned int capacity)
{
    bool ext_lcd_supported = false;
    int is_float = 0;
    if (MTK_LOG_ENABLE == 1)
    {
        SLOGD("[libshowlogo: %s %d]capacity =%d\n", __FUNCTION__, __LINE__, capacity);
    }
    show_animationm_ver = VERION_NEW_ANIMATION;
    if(chargingAnimationDimension.IS_DRM == -1) {
        SLOGE("[libshowlogo: %s %d]DRM was failed. no animation will be there \n", __FUNCTION__, __LINE__);
        return;
    }
    ext_lcd_supported = is_ext_lcd_present();
    int has_fast_charging = getValue("ro.vendor.mtk_fast_charging_support", "0");

    SLOGD("[libshowlogo: %s %d]has_fast_charging =%d\n", __FUNCTION__, __LINE__, has_fast_charging);

    if (draw_anim_mode == (DRAW_ANIM_MODE_FB))
    {
        SLOGD("[libshowlogo: %s %d]draw_anim_mode =%d\n", __FUNCTION__, __LINE__, draw_anim_mode);
        anim_fb_addr_switch();
        if (has_fast_charging == 1)
        {
            if (2 != show_animationm_ver && get_fast_charging_state())
            {
                fill_animation_battery_by_ver(capacity, (void *)fb_addr, dec_logo_addr, logo_addr, phical_screen, show_animationm_ver);

                if (ext_lcd_supported) //2nd lcd present
                    fill_animation_battery_by_ver(capacity, (void *)fb_addr_ext, dec_logo_addr, logo_addr, phical_screen_ext, draw_anim_mode);
            }
            else
            {
                fill_animation_battery_by_ver(capacity, (void *)fb_addr, dec_logo_addr, logo_addr, phical_screen, show_animationm_ver);

                if (ext_lcd_supported) //2nd lcd present
                    fill_animation_battery_by_ver(capacity, (void *)fb_addr_ext, dec_logo_addr, logo_addr, phical_screen_ext, draw_anim_mode);
            }
	    } else {
            fill_animation_battery_by_ver(capacity, (void *)fb_addr, dec_logo_addr, logo_addr, phical_screen, show_animationm_ver);

            if (ext_lcd_supported) //2nd lcd present
                fill_animation_battery_by_ver(capacity, (void *)fb_addr_ext, dec_logo_addr, logo_addr, phical_screen_ext, draw_anim_mode);
        }

        if (chargingAnimationDimension.IS_DRM == 1)
            anim_drm_disp_update();
        else
            anim_fb_disp_update();
    }
    else
    {
        ARect tmpRect, tmpRect_ext;

        tmpRect.left = 0;
        tmpRect.top = 0;
        tmpRect.right = phical_screen.width;
        tmpRect.bottom = phical_screen.height;

        tmpRect_ext.left = 0;
        tmpRect_ext.top = 0;
        tmpRect_ext.right = phical_screen.width;
        tmpRect_ext.bottom = phical_screen.height;

        status_t lockResult = surface->lock(&outBuffer, &tmpRect);
        status_t lockResult_ext = surface_ext->lock(&outBuffer_ext, &tmpRect_ext);
        if (MTK_LOG_ENABLE == 1)
        {
            SLOGD("[libshowlogo: %s %d]outBuffer.bits = %ld, surface->lock return =  0x%08x,\n", __FUNCTION__, __LINE__, (long)outBuffer.bits, lockResult);
            SLOGD("[libshowlogo: %s %d]outBuffer_ext.bits = %ld, surface->lock return =  0x%08x,\n", __FUNCTION__, __LINE__, (long)outBuffer_ext.bits, lockResult_ext);
        }
        if (0 == lockResult)
        {
			if (has_fast_charging == 1)
            {
                if (2 != show_animationm_ver && get_fast_charging_state())
                {
                    fill_animation_battery_fast_charging(capacity, (void *)outBuffer.bits, dec_logo_addr, logo_addr, phical_screen, draw_anim_mode);
                    fill_animation_battery_fast_charging(capacity, (void *)outBuffer_ext.bits, dec_logo_addr, logo_addr, phical_screen_ext, draw_anim_mode);
                }
                else
                {
                    fill_animation_battery_by_ver(capacity, (void *)outBuffer.bits, dec_logo_addr, logo_addr, phical_screen, show_animationm_ver);
                    fill_animation_battery_by_ver(capacity, (void *)outBuffer_ext.bits, dec_logo_addr, logo_addr, phical_screen_ext, show_animationm_ver);
                }
            }
            else
            {
                fill_animation_battery_by_ver(capacity, (void *)outBuffer.bits, dec_logo_addr, logo_addr, phical_screen, show_animationm_ver);
                fill_animation_battery_by_ver(capacity, (void *)outBuffer_ext.bits, dec_logo_addr, logo_addr, phical_screen_ext, show_animationm_ver);
            }
            surface->unlockAndPost();
            surface_ext->unlockAndPost();
        }
    }
}

/* Sycamore code for SYCAMORE-179 by kangkai4 at 20241120 end */
/*
 * show vitural word prompt:
 *     1.calculate the starting coordinates
 *     2.traverse each pixel，copy to the corresponding
 *     position of the framebuffer.
 */
void display_bypass_logo_word(unsigned int* fb, int screen_w, int screen_h)
{
    int width_word = LOGO_WIDTH_EN;
    int height_word = LOGO_HEIGHT_EN;
    unsigned int start_x_word = (screen_w - width_word) / 2 - (LOGO_WIDTH+width_word)/2 - 70;
    unsigned int start_y_word = (screen_h - height_word) / 2;
    for (int y = 0; y < height_word; y++) {
        for (int x = 0; x < width_word; x++) {
            unsigned int src_idx_word = (y * width_word + x) * 4;
            unsigned int dst_x_word = start_x_word + x;
            unsigned int dst_y_word = start_y_word + y;
            unsigned int dst_idx_word = (dst_y_word * screen_w + dst_x_word);
            memcpy(&fb[dst_idx_word], &g_word_logo_argb_en[src_idx_word], 4);
        }
    }
}
/*
 * show vitural animation:
 *     1.calculate the starting coordinates
 *     2.traverse each pixel，copy to the corresponding
 *     position of the framebuffer.
 */
void display_bypass_logo(void)
{
    if(draw_anim_mode == DRAW_ANIM_MODE_SURFACE)
        draw_anim_mode = DRAW_ANIM_MODE_FB;
    unsigned int *fb = charging_fb_addr;
    if (!fb) {
        SLOGD("[LOGO] Failed to get VRAM base\n");
        return;
    }
    SLOGD("kpoc show bypass logo charging %d,fb = %p\n",__LINE__, fb);
    // get the actual screen resolution to prevent the logo from exceeding the boundary
    unsigned int screen_w = phical_screen.width;//1600
    unsigned int screen_h = phical_screen.height;//2560
    for (int i = 0; i < screen_h * screen_w; i++) {
        fb[i] = 0xff000000;
    }
    anim_drm_disp_update();
    // match size
    if (LOGO_WIDTH != screen_w || LOGO_HEIGHT != screen_h) {
        SLOGD("[LOGO] Size mismatch: logo=%dx%d!, screen=%dx%d\n",
                LOGO_WIDTH, LOGO_HEIGHT, screen_w, screen_h);
    }
    int width = LOGO_WIDTH;
    int height = LOGO_HEIGHT;
    unsigned int start_x = (screen_w - LOGO_WIDTH) / 2;
    unsigned int start_y = (screen_h - LOGO_HEIGHT) / 2;
    for (int y = 0; y < LOGO_HEIGHT; y++) {
        for (int x = 0; x < LOGO_WIDTH; x++) {
            // calculate offset of each pixel in picture
            unsigned int src_idx = (y * LOGO_WIDTH + x) * 4; // 4 bits in each pixel (32)
            //calculate picture's pixel location in current screen
            unsigned int dst_x = start_x + x;
            unsigned int dst_y = start_y + y;
            unsigned int dst_idx = (dst_y * screen_w + dst_x);//!!
            // copy pixel from picture data to framebuffer(screen)
            memcpy(&fb[dst_idx], &g_logo_argb[src_idx], 4);
        }
    }
    display_bypass_logo_word(fb, screen_w, screen_h);
    // flash display
    anim_drm_disp_update();
    SLOGD("[LOGO] Hardcoded logo displayed\n");
    if(draw_anim_mode == DRAW_ANIM_MODE_FB)
        draw_anim_mode = DRAW_ANIM_MODE_SURFACE;
}

/*
 * Show fast charging animation with battery capacity
 * code for test fast charging
 */
void show_fast_charging(unsigned int capacity)
{
    if (MTK_LOG_ENABLE == 1)
    {
        SLOGD("[libshowlogo: %s %d]capacity =%d\n", __FUNCTION__, __LINE__, capacity);
    }
    if (draw_anim_mode == (DRAW_ANIM_MODE_FB))
    {
        anim_fb_addr_switch();
        fill_animation_battery_fast_charging(capacity, (void *)fb_addr, dec_logo_addr, logo_addr, phical_screen, draw_anim_mode);

        if (is_ext_lcd_present()) //2nd lcd present
            fill_animation_battery_fast_charging(capacity, (void *)fb_addr_ext, dec_logo_addr, logo_addr, phical_screen_ext, draw_anim_mode);

        if (chargingAnimationDimension.IS_DRM == 1)
            anim_drm_disp_update();
        else
            anim_fb_disp_update();
    }
    else
    {
        ARect tmpRect, tmpRect_ext;
        tmpRect.left = 0;
        tmpRect.top = 0;
        tmpRect.right = phical_screen.width;
        tmpRect.bottom = phical_screen.height;
        tmpRect_ext.left = 0;
        tmpRect_ext.top = 0;
        tmpRect_ext.right = phical_screen_ext.width;
        tmpRect_ext.bottom = phical_screen_ext.height;
        status_t lockResult = surface->lock(&outBuffer, &tmpRect);
        status_t lockResult_ext = surface_ext->lock(&outBuffer_ext, &tmpRect_ext);
        if (MTK_LOG_ENABLE == 1)
        {
            SLOGD("[libshowlogo: %s %d]LCD 1 - outBuffer.bits = %ld, surface->lock return =  0x%08x,\n", __FUNCTION__, __LINE__, (long)outBuffer.bits, lockResult);
            SLOGD("[libshowlogo: %s %d]LCD 2 - ext outBuffer.bits = %ld, surface->lock return =  0x%08x,\n", __FUNCTION__, __LINE__, (long)outBuffer_ext.bits, lockResult_ext);
        }
        if (0 == lockResult)
        {
            fill_animation_battery_fast_charging(capacity, (void *)outBuffer.bits, dec_logo_addr, logo_addr, phical_screen, draw_anim_mode);
            fill_animation_battery_fast_charging(capacity, (void *)outBuffer_ext.bits, dec_logo_addr, logo_addr, phical_screen_ext, draw_anim_mode);

            surface->unlockAndPost();
            surface_ext->unlockAndPost();
        }
    }
}

int anim_drm_pre_init()
{
    int res = 0;

    SLOGD("[libshowlogo: %s %d] drm_resource_object = %p  \n", __FUNCTION__, __LINE__, &drm_resource_object);
    res = init_resource(&drm_resource_object);

    if(res != 0) {
        SLOGD("[libshowlogo: %s %d] drm_resource pre_init failed. \n", __FUNCTION__, __LINE__);
        return res;
    }

    SLOGD("[libshowlogo: %s %d] drm_resource_successfully initialised \n", __FUNCTION__, __LINE__);
    SLOGD("[libshowlogo: %s %d] drm_resource_object = %p mconnector = %p  \n",
          __FUNCTION__, __LINE__, &drm_resource_object, &(drm_resource_object.mConnector));

    SLOGD("[libshowlogo: %s %d] drm_resource_object = %p mconnector = %p mconnector_ext = %p \n",
          __FUNCTION__, __LINE__, &drm_resource_object,
          &(drm_resource_object.mConnector), &(drm_resource_object.mConnector_ext));

    // map drmResouce to libdrm structure in charging animation.h
    libdrmApi.DrmInfo = drm_resource_object.mConnector->mModes[0].mInfo;
    libdrmApi.DrmFb = &(drm_resource_object.fb);
    SLOGD("[libshowlogo: %s %d] LCD 1 drmfb saved bpp=%d", __FUNCTION__, __LINE__, drm_resource_object.fb.bpp);

    // set macro to be used in show_animaton_common
    chargingAnimationDimension.HDISPLAY = libdrmApi.DrmInfo.hdisplay;
    chargingAnimationDimension.VDISPLAY = libdrmApi.DrmInfo.vdisplay;
    chargingAnimationDimension.FB_BPP = drm_resource_object.fb.bpp;
    chargingAnimationDimension.FB_WIDTH = drm_resource_object.fb.width;
    chargingAnimationDimension.IS_DRM = 1;
    SLOGD("[libshowlogo: %s %d] LCD 1 hdisplay = %d  vdisplay = %d fb->width = %d\n",
          __FUNCTION__, __LINE__, chargingAnimationDimension.HDISPLAY,
          chargingAnimationDimension.VDISPLAY, drm_resource_object.fb.width);

    //dualdisplay - set parameters for 2nd LCD
    if (is_ext_lcd_present()) //2nd lcd present
    {
      libdrmApi_ext.DrmInfo = drm_resource_object.mConnector_ext->mModes[0].mInfo;
      libdrmApi_ext.DrmFb = &(drm_resource_object.fb_ext);
      SLOGD("[libshowlogo: %s %d] LCD 2 drmfb saved bpp=%d", __FUNCTION__, __LINE__, drm_resource_object.fb.bpp);
      // set macro to be used in show_animaton_common
      chargingAnimationDimension_ext.HDISPLAY = libdrmApi_ext.DrmInfo.hdisplay;
      chargingAnimationDimension_ext.VDISPLAY = libdrmApi_ext.DrmInfo.vdisplay;
      chargingAnimationDimension_ext.FB_BPP = drm_resource_object.fb_ext.bpp;
      chargingAnimationDimension_ext.FB_WIDTH = drm_resource_object.fb_ext.width;
      chargingAnimationDimension_ext.IS_DRM = 1;
      SLOGD("[libshowlogo: %s %d] LCD 2 hdisplay = %d  vdisplay = %d fb->width = %d\n",
            __FUNCTION__, __LINE__, chargingAnimationDimension_ext.HDISPLAY,
            chargingAnimationDimension_ext.VDISPLAY, drm_resource_object.fb_ext.width);
    }

    return res;
}

void init_drm_addresses(void)
{
    if (MTK_LOG_ENABLE == 1)
    {
        SLOGD("[libshowlogo: %s %d]\n", __FUNCTION__, __LINE__);
    }
    drm_fd = drm_resource_object.mDrmFd;
    SLOGD("[libshowlogo: %s %d] drm_fd = %d bpp = %d bo->fd = %d", __FUNCTION__, __LINE__, drm_fd, chargingAnimationDimension.FB_BPP, drm_resource_object.mBo[drm_resource_object.mPinPon].fd);
    fb_size = (chargingAnimationDimension.HDISPLAY * (chargingAnimationDimension.FB_BPP / 8)) * chargingAnimationDimension.VDISPLAY;
    SLOGD("[libshowlogo: %s %d] fb_size = %u", __FUNCTION__, __LINE__, fb_size);

    dec_logo_addr = (unsigned int *)malloc(fb_size);

    if (dec_logo_addr == NULL)
    {
        if (MTK_LOG_ENABLE == 1)
        {
            SLOGE("[libshowlogo: %s %d]allocate dec_logo_addr buffer fail \n", __FUNCTION__, __LINE__);
        }
        assert(0);
    }
    SLOGD("[libshowlogo: %s %d] dec_logo_addr = %p", __FUNCTION__, __LINE__, dec_logo_addr);
    lk_fb_addr = (unsigned int *)drm_mmap(0, fb_size, PROT_READ | PROT_WRITE, MAP_SHARED, drm_resource_object.mBo[drm_resource_object.mPinPon].fd, 0);
    SLOGD("[libshowlogo: %s %d] lk_fb_addr = %p %u", __FUNCTION__, __LINE__, lk_fb_addr, lk_fb_addr);
    charging_fb_addr = (unsigned int *)drm_mmap(0, fb_size, PROT_READ | PROT_WRITE, MAP_SHARED, drm_resource_object.mBo[drm_resource_object.mPinPon].fd, 0);
    SLOGD("[libshowlogo: %s %d] charging_fb_addr = %p %u", __FUNCTION__, __LINE__, charging_fb_addr, charging_fb_addr);
    kernel_fb_addr = (unsigned int *)drm_mmap(0, fb_size, PROT_READ | PROT_WRITE, MAP_SHARED, drm_resource_object.mBo[drm_resource_object.mPinPon].fd, 0);
    SLOGD("[libshowlogo: %s %d] kernel_fb_addr = %p %u", __FUNCTION__, __LINE__, kernel_fb_addr, kernel_fb_addr);
    fb_addr = lk_fb_addr;
    SLOGD("[libshowlogo: %s %d] fb_addr = %p %u", __FUNCTION__, __LINE__, fb_addr, fb_addr);

    if (MTK_LOG_ENABLE == 1)
    {
        SLOGD("[libshowlogo: %s %d]vinfo:xres  = %d, yres = %d, xres_virtual =%d, bits_per_pixel = %d,red.offset = %d,blue.offset = %d\n", __FUNCTION__, __LINE__, chargingAnimationDimension.HDISPLAY, chargingAnimationDimension.VDISPLAY, chargingAnimationDimension.FB_WIDTH, chargingAnimationDimension.FB_BPP, 8, 24);
        // in drm, bo format -> argb8888 , red.offset = 8 and blue.offset =  24
        SLOGD("[libshowlogo: %s %d]fb_size =%d, fb_addr = %ld,charging_fb_addr=%ld\n", __FUNCTION__, __LINE__, fb_size, (long)fb_addr, (long)charging_fb_addr);
    }

    //dualdisplay - start
    if (is_ext_lcd_present()) //2nd lcd present
    {
      fb_size_ext = (chargingAnimationDimension_ext.HDISPLAY * (chargingAnimationDimension_ext.FB_BPP / 8)) * chargingAnimationDimension_ext.VDISPLAY;
      SLOGD("[libshowlogo: %s %d] fb_size_ext = %u", __FUNCTION__, __LINE__, fb_size_ext);
      lk_fb_addr_ext = (unsigned int *)drm_mmap(0, fb_size_ext, PROT_READ | PROT_WRITE, MAP_SHARED, drm_resource_object.mBo_ext[drm_resource_object.mPinPon_ext].fd, 0);
      SLOGD("[libshowlogo: %s %d] lk_fb_addr_ext = %p %u", __FUNCTION__, __LINE__, lk_fb_addr_ext, lk_fb_addr_ext);
      charging_fb_addr_ext = (unsigned int *)drm_mmap(0, fb_size_ext, PROT_READ | PROT_WRITE, MAP_SHARED, drm_resource_object.mBo_ext[drm_resource_object.mPinPon_ext].fd, 0);
      SLOGD("[libshowlogo: %s %d] charging_fb_addr_ext = %p %u", __FUNCTION__, __LINE__, charging_fb_addr_ext, charging_fb_addr_ext);
      kernel_fb_addr_ext = (unsigned int *)drm_mmap(0, fb_size_ext, PROT_READ | PROT_WRITE, MAP_SHARED, drm_resource_object.mBo_ext[drm_resource_object.mPinPon_ext].fd, 0);
      SLOGD("[libshowlogo: %s %d] kernel_fb_addr_ext = %p %u", __FUNCTION__, __LINE__, kernel_fb_addr_ext, kernel_fb_addr_ext);
      fb_addr_ext = lk_fb_addr_ext;
      SLOGD("[libshowlogo: %s %d] fb_addr_ext = %p %u", __FUNCTION__, __LINE__, fb_addr_ext, fb_addr_ext);
      if (MTK_LOG_ENABLE == 1)
      {
          SLOGD("[libshowlogo: %s %d]vinfo_ext:xres  = %d, yres = %d, xres_virtual =%d, bits_per_pixel = %d,red.offset = %d,blue.offset = %d\n",
                __FUNCTION__, __LINE__, chargingAnimationDimension_ext.HDISPLAY, chargingAnimationDimension_ext.VDISPLAY,
                chargingAnimationDimension_ext.FB_WIDTH, chargingAnimationDimension_ext.FB_BPP, 8, 24);
          // in drm, bo format -> argb8888 , red.offset = 8 and blue.offset =  24
          SLOGD("[libshowlogo: %s %d]fb_size_ext =%d, fb_addr_ext = %ld,charging_fb_addr_ext=%ld\n", __FUNCTION__, __LINE__, fb_size_ext, (long)fb_addr_ext, (long)charging_fb_addr_ext);
      }
    }
    //dualdisplay - end

    if (fb_addr == NULL || charging_fb_addr == NULL)
    {
        if (MTK_LOG_ENABLE == 1)
        {
            SLOGE("ChargingAnimation mmap fail\n");
        }
        drm_munmap(charging_fb_addr, fb_size);
        drm_munmap(lk_fb_addr, fb_size);
        //2nd lcd present
        if ((is_ext_lcd_present()) && (fb_addr_ext == NULL || charging_fb_addr_ext == NULL))
        {
          drm_munmap(charging_fb_addr_ext, fb_size_ext);
          drm_munmap(lk_fb_addr_ext, fb_size_ext);
        }
        close(drm_fd);
        error_flag = 1;
    }
}

void free_drm_address(void) {
    if (MTK_LOG_ENABLE == 1)
    {
        SLOGD("[libshowlogo: %s %d]\n", __FUNCTION__, __LINE__);
    }
    struct DrmBo *bo = &(drm_resource_object.mBo[drm_resource_object.mPinPon]);

    if (kernel_fb_addr != NULL)
        drm_munmap(kernel_fb_addr, bo->size);
    if (charging_fb_addr != NULL)
        drm_munmap(charging_fb_addr, bo->size);
    if (lk_fb_addr != NULL)
        drm_munmap(lk_fb_addr, bo->size);

    if (is_ext_lcd_present())
    {
      struct DrmBo *bo_ext = &(drm_resource_object.mBo_ext[drm_resource_object.mPinPon_ext]);

      if (kernel_fb_addr_ext != NULL)
          drm_munmap(kernel_fb_addr_ext, bo_ext->size);
      if (charging_fb_addr_ext != NULL)
          drm_munmap(charging_fb_addr_ext, bo_ext->size);
      if (lk_fb_addr_ext != NULL)
          drm_munmap(lk_fb_addr_ext, bo_ext->size);
    }
}

int anim_drm_init(void)
{
    if (MTK_LOG_ENABLE == 1)
    {
        SLOGD("[libshowlogo: %s %d]\n", __FUNCTION__, __LINE__);
    }
    init_drm_addresses();
    phical_screen.bits_per_pixel = chargingAnimationDimension.FB_BPP;
    phical_screen.fill_dst_bits = chargingAnimationDimension.FB_BPP;
    phical_screen.red_offset = 0;
    phical_screen.blue_offset = 16;
    phical_screen.width = chargingAnimationDimension.HDISPLAY;
    phical_screen.height = chargingAnimationDimension.VDISPLAY;
    phical_screen.allignWidth = fb_size / (((chargingAnimationDimension.FB_BPP / 8) * chargingAnimationDimension.VDISPLAY));
    phical_screen.needAllign = 1;
    phical_screen.need180Adjust = 1;
    phical_screen.fb_size = fb_size;

    int rotation = getRotation();
    if (MTK_LOG_ENABLE == 1)
      SLOGD("[libshowlogo: %s %d]rotation = %d\n", __FUNCTION__, __LINE__, rotation);

    if (ORIENTATION_270 == rotation) //270
      phical_screen.rotation = 270;
    else if (ORIENTATION_90 == rotation) //90
      phical_screen.rotation = 90;
    else if ((ORIENTATION_180 == rotation) && (phical_screen.need180Adjust == 1)) //180
      phical_screen.rotation = 180;
    else
      phical_screen.rotation = 0;

    if (MTK_LOG_ENABLE == 1)
    {
        SLOGD("[libshowlogo: %s %d]LCD 1 - MTK_LCM_PHYSICAL_ROTATION = %s , red_offset = %d , blue_offset =  %d\n align_width = %d",
              __FUNCTION__, __LINE__, MTK_LCM_PHYSICAL_ROTATION, phical_screen.red_offset, phical_screen.blue_offset, phical_screen.allignWidth);
        SLOGD("[libshowlogo]phical_screen: width= %d,height= %d,bits_per_pixel =%d,needAllign = %d,allignWidth=%d rotation =%d ,need180Adjust = %d\n",
              phical_screen.width, phical_screen.height,
              phical_screen.bits_per_pixel, phical_screen.needAllign,
              phical_screen.allignWidth, phical_screen.rotation, phical_screen.need180Adjust);
        SLOGD("[libshowlogo: %s %d]show old animtion= 1, running show_animationm_ver %d\n", __FUNCTION__, __LINE__, show_animationm_ver);
        SLOGD("[libshowlogo: %s %d]draw_anim_mode = 1, running mode %d\n", __FUNCTION__, __LINE__, draw_anim_mode);
    }

    //dualdisplay - start
    if (is_ext_lcd_present()) //2nd lcd present
    {
      phical_screen_ext.bits_per_pixel = chargingAnimationDimension_ext.FB_BPP;
      phical_screen_ext.fill_dst_bits = chargingAnimationDimension_ext.FB_BPP;
      phical_screen_ext.red_offset = 0;
      phical_screen_ext.blue_offset = 16;
      phical_screen_ext.width = chargingAnimationDimension_ext.HDISPLAY;
      phical_screen_ext.height = chargingAnimationDimension_ext.VDISPLAY;
      phical_screen_ext.allignWidth = fb_size_ext / (((chargingAnimationDimension_ext.FB_BPP / 8) * chargingAnimationDimension_ext.VDISPLAY));
      phical_screen_ext.needAllign = 1;
      phical_screen_ext.need180Adjust = 0;
      phical_screen_ext.fb_size = fb_size_ext;
      //dualdisplay - 2nd LCD not to support rotation, hence no need to set it for 2nd LCD, it will be zero (0) always
      phical_screen_ext.rotation = 0;

      if (MTK_LOG_ENABLE == 1)
        SLOGD("[libshowlogo: %s %d]LCD 2 - MTK_LCM_PHYSICAL_ROTATION = %s , red_offset_ext = %d , blue_offset_ext =  %d\n align_width_ext = %d",
              __FUNCTION__, __LINE__, MTK_LCM_PHYSICAL_ROTATION, phical_screen_ext.red_offset, phical_screen_ext.blue_offset, phical_screen_ext.allignWidth);
    }
    //dualdisplay - end

    if (MTK_LOG_ENABLE == 1)
    {
        //dualdisplay - for 2nd LCD
        SLOGD("[libshowlogo]phical_screen_ext: width= %d,height= %d,bits_per_pixel =%d,needAllign = %d,allignWidth=%d rotation =%d ,need180Adjust = %d\n",
              phical_screen_ext.width, phical_screen_ext.height,
              phical_screen_ext.bits_per_pixel, phical_screen_ext.needAllign,
              phical_screen_ext.allignWidth, phical_screen_ext.rotation, phical_screen_ext.need180Adjust);
    }

    return 0;
}

void anim_drm_deinit()
{
    if (MTK_LOG_ENABLE == 1)
    {
        SLOGD("[libshowlogo: %s %d]\n", __FUNCTION__, __LINE__);
    }
    struct DrmBo *bo = &(drm_resource_object.mBo[drm_resource_object.mPinPon]);
    struct DrmBo *bo_ext = &(drm_resource_object.mBo_ext[drm_resource_object.mPinPon_ext]);

    if (kernel_fb_addr != NULL)
        drm_munmap(kernel_fb_addr, bo->size);
    if (charging_fb_addr != NULL)
        drm_munmap(charging_fb_addr, bo->size);
    if (lk_fb_addr != NULL)
        drm_munmap(lk_fb_addr, bo->size);

    if (drm_resource_object.mCrtc_ext != NULL)
    {
      if (kernel_fb_addr_ext != NULL)
          drm_munmap(kernel_fb_addr_ext, bo_ext->size);
      if (charging_fb_addr_ext != NULL)
          drm_munmap(charging_fb_addr_ext, bo_ext->size);
      if (lk_fb_addr_ext != NULL)
          drm_munmap(lk_fb_addr_ext, bo_ext->size);
    }

    freeResource(&drm_resource_object);
    close(drm_fd);
}

void anim_drm_disp_update(void)
{
    int res, res_ext = 0;
    uint32_t connectorId = getConnectorId(*(drm_resource_object.mConnector));
    drmModeModeInfo modeInfo = getModeInfo(drm_resource_object.mConnector->mModes[0]);
    uint32_t crtcId = getCrtcId(*(drm_resource_object.mCrtc));
    struct DrmBo *bo = &(drm_resource_object.mBo[drm_resource_object.mPinPon]);

    if (MTK_LOG_ENABLE == 1)
    {
        SLOGD("[libshowlogo: %s %d]\n", __FUNCTION__, __LINE__);
        SLOGD("[libshowlogo: %s %d] if reach here, everything was successfull in libdrm \n", __FUNCTION__, __LINE__);
        SLOGD("[libshowlogo %s %d] drm_resource_object = %p ", __FUNCTION__, __LINE__, drm_resource_object);
    }

    if (MTK_LOG_ENABLE == 1)
    {
        SLOGD("[libshowlogo %s %d] drmmodeinfo got success", __FUNCTION__, __LINE__);
        SLOGD("[libshowlogo %s %d] LCD 1 - data for crtc => mdrmfd = %d ,\n crtcId = %d,\n fbId = %d,\n x= %d,\n Y = %d,\n connectorId = %p ,\n 1,\n modeInfo = %p %p",
              __FUNCTION__, __LINE__, drm_resource_object.mDrmFd, crtcId, bo->fbId, 0, 0, &connectorId, &modeInfo, modeInfo);
        SLOGD("[drm_resource %s %d] LCD 1 - drmmodeinfo before setCrtc = %d,\n hdisplay = %d, hsync_start = %d, hsync_end = %d, htotal = %d, hskew = %d, \n vdisplay = %d, vsync_start = %d, vsync_end = %d, vtotal = %d, vscan = %d, \n vrefresh = %d, flags = %d, type = %d",
            __FUNCTION__, __LINE__, modeInfo.clock, modeInfo.hdisplay, modeInfo.hsync_start, modeInfo.hsync_end, modeInfo.htotal, modeInfo.hskew, modeInfo.vdisplay, modeInfo.vsync_start, modeInfo.vsync_end, modeInfo.vtotal, modeInfo.vtotal, modeInfo.vscan, modeInfo.vrefresh, modeInfo.flags, modeInfo.type);
    }
    res = drmModeSetCrtc(drm_resource_object.mDrmFd, crtcId, bo->fbId, 0, 0, &connectorId, 1, &modeInfo);
    
    if (res != 0)
        SLOGE("[libshowlogo %s %d] LCD 1 - failed to set crtc: %d", __FUNCTION__, __LINE__, res);
    else
        SLOGD("[libshowlogo %s %d] LCD 1 - draw a frame [%zu]fb_id=%u crtc=%u conn=%u ", __FUNCTION__, __LINE__, drm_resource_object.mPinPon, drm_resource_object.mBo[drm_resource_object.mPinPon].fbId, crtcId, connectorId);

    if (drm_resource_object.mCrtc_ext != NULL)
    {
        uint32_t connectorId_ext = getConnectorId(*(drm_resource_object.mConnector_ext));
        drmModeModeInfo modeInfo_ext = getModeInfo(drm_resource_object.mConnector_ext->mModes[0]);
        uint32_t crtcId_ext = getCrtcId(*(drm_resource_object.mCrtc_ext));
        struct DrmBo *bo_ext = &(drm_resource_object.mBo_ext[drm_resource_object.mPinPon_ext]);

        res_ext = drmModeSetCrtc(drm_resource_object.mDrmFd, crtcId_ext, bo_ext->fbId, 0, 0, &connectorId_ext, 1, &modeInfo_ext);
        if (res_ext != 0)
            SLOGE("[libshowlogo %s %d] LCD 2 - failed to set crtc: %d", __FUNCTION__, __LINE__, res);
        else
            SLOGD("[libshowlogo %s %d] LCD 2 - draw a frame [%zu]fb_id=%u crtc=%u conn=%u ", __FUNCTION__, __LINE__, drm_resource_object.mPinPon_ext, drm_resource_object.mBo_ext[drm_resource_object.mPinPon_ext].fbId, crtcId_ext, connectorId_ext);

        if (MTK_LOG_ENABLE == 1)
        {
            SLOGD("[libshowlogo %s %d] LCD 2 - data for crtc => mdrmfd = %d ,\n crtcId = %d,\n fbId = %d,\n x= %d,\n Y = %d,\n connectorId = %p ,\n 1,\n modeInfo = %p %p",
                  __FUNCTION__, __LINE__, drm_resource_object.mDrmFd, crtcId_ext, bo_ext->fbId, 0, 0, &connectorId_ext, &modeInfo_ext, modeInfo_ext);
            SLOGD("[drm_resource %s %d] LCD 2 - drmmodeinfo before setCrtc = %d,\n hdisplay = %d, hsync_start = %d, hsync_end = %d, htotal = %d, hskew = %d, \n vdisplay = %d, vsync_start = %d, vsync_end = %d, vtotal = %d, vscan = %d, \n vrefresh = %d, flags = %d, type = %d",
                __FUNCTION__, __LINE__, modeInfo_ext.clock, modeInfo_ext.hdisplay, modeInfo_ext.hsync_start, modeInfo_ext.hsync_end, modeInfo_ext.htotal, modeInfo_ext.hskew, modeInfo_ext.vdisplay, modeInfo_ext.vsync_start, modeInfo_ext.vsync_end, modeInfo_ext.vtotal, modeInfo_ext.vtotal, modeInfo_ext.vscan, modeInfo_ext.vrefresh, modeInfo_ext.flags, modeInfo_ext.type);
        }
    }

    free_drm_address();
    drm_resource_object.mPinPon = (drm_resource_object.mPinPon + 1) % MAX_BO_SIZE;

    if (drm_resource_object.mCrtc_ext != NULL)
        drm_resource_object.mPinPon_ext = (drm_resource_object.mPinPon_ext + 1) % MAX_BO_SIZE;

    init_drm_addresses();
    SLOGD("[libshowlogo: %s %d] LCD 1 - drm dropped  nextBuffer = %d\n", __FUNCTION__, __LINE__, drm_resource_object.mPinPon);

    if (drm_resource_object.mCrtc_ext != NULL)
      SLOGD("[libshowlogo: %s %d] LCD 2 - drm dropped  nextBuffer = %d\n", __FUNCTION__, __LINE__, drm_resource_object.mPinPon_ext);
}

/* to check whether DRM is supported or not */
int is_drm()
{
    return chargingAnimationDimension.IS_DRM;
}

/* to turn of KPOC in DRM */

void turn_off_drm_display(void)
{
    SLOGD("[libshowlogo %s %d] Turning off KPOC display \n", __FUNCTION__, __LINE__);
    uint32_t connectorId = getConnectorId(*(drm_resource_object.mConnector));
    uint32_t crtcId = getCrtcId(*(drm_resource_object.mCrtc));
    int res = drmModeSetCrtc(drm_resource_object.mDrmFd, crtcId, 0, 0, 0, nullptr, 0, nullptr);

    if (res != 0)
    {
        SLOGE("[libshowlogo %s %d] LCD 1 - failed to set crtc: %d \n", __FUNCTION__, __LINE__, res);
    }
    else
    {
        SLOGD("[libshowlogo %s %d] LCD 1 - draw a frame [%zu]fb_id=%u crtc=%u conn=%u \n",
              __FUNCTION__, __LINE__, drm_resource_object.mPinPon, drm_resource_object.mBo[drm_resource_object.mPinPon].fbId, crtcId, connectorId);
    }

    if (is_ext_lcd_present()) //2nd lcd present
    {
      uint32_t connectorId_ext = getConnectorId(*(drm_resource_object.mConnector_ext));
      uint32_t crtcId_ext = getCrtcId(*(drm_resource_object.mCrtc_ext));
      int res_ext = drmModeSetCrtc(drm_resource_object.mDrmFd, crtcId_ext, 0, 0, 0, nullptr, 0, nullptr);
      if (res_ext != 0)
      {
          SLOGE("[libshowlogo %s %d] LCD 2 - failed to set crtc: %d \n", __FUNCTION__, __LINE__, res_ext);
      }
      else
      {
          SLOGD("[libshowlogo %s %d] LCD 2 - draw a frame [%zu]fb_id=%u crtc=%u conn=%u \n",
                __FUNCTION__, __LINE__, drm_resource_object.mPinPon_ext, drm_resource_object.mBo_ext[drm_resource_object.mPinPon_ext].fbId, crtcId_ext, connectorId_ext);
      }
    }

    free_drm_address();
    init_drm_addresses();
    SLOGD("[libshowlogo: %s %d]] Turning off KPOC display completed \n", __FUNCTION__, __LINE__);
}

int get_ext_lcd_width(void)
{
    if (is_ext_lcd_present())
    {
        if (MTK_LOG_ENABLE == 1)
          SLOGD("[libshowlogo %s %d] ext lcd width = %d\n", __FUNCTION__, __LINE__, phical_screen_ext.width);

        return phical_screen_ext.width;
    }
    else
        return 0;
}

int get_ext_lcd_height(void)
{
    if (is_ext_lcd_present())
    {
        if (MTK_LOG_ENABLE == 1)
            SLOGD("[libshowlogo %s %d] ext lcd height = %d\n", __FUNCTION__, __LINE__, phical_screen_ext.height);

        return phical_screen_ext.height;
    }
    else
        return 0;
}

bool get_logo_multibin_status(void)
{
    if (MTK_LOG_ENABLE == 1)
        SLOGD("[libshowlogo %s %d] multibin status = %d\n", __FUNCTION__, __LINE__, is_multibin);
    return is_multibin;
}

void set_logo_multibin_status(bool bin_status)
{
    if (MTK_LOG_ENABLE == 1)
        SLOGD("[libshowlogo %s %d] multibin status = %d\n", __FUNCTION__, __LINE__, bin_status);
    is_multibin = bin_status;
}