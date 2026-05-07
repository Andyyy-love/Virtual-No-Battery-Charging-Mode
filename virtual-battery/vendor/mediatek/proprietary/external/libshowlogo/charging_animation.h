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

#ifndef _CHARGING_ANIMATION_H
#define _CHARGING_ANIMATION_H

#include "show_logo_common.h"
#include "show_animation_common.h"

#ifdef __cplusplus
extern "C" {
#endif

// including drm to replace fb_dev
#include "drm_resource.h"

// logo.bin image max size
#define LOGO_BUFFER_SIZE    0x2000000

#define FB_NODE_PATH   "/dev/graphics/fb0"
//M: Currently disabling battery path.. in later stage we will update in code to check path by order. @{ 
//#ifdef MTK_GM_30
#define CHARGER_FAST_PATH "/sys/devices/platform/charger/usb_type"
#define FLOAT_TYPE_FLAG_PATH "/sys/devices/platform/charger/usb_type"
#define BATTERY_TEMPERATURE_PATH "/sys/devices/platform/charger/battery_temp"
#define CHARGER_ONLINE_PATH "/sys/devices/platform/charger/battery_status"
#define BATTERY_PRESENT_PATH "/sys/devices/platform/charger/battery_present"
#define BYPASS_SETTING "/sys/devices/platform/11016000.i2c/i2c-5/5-0055/power_supply/mm8013c10-0/device/bypass_setting"

//#else
//#define CHARGER_FAST_PATH "/sys/devices/platform/battery/Pump_Express"
//#endif
//M: @}

/********** draw_anim_mode:  charging animation drawing method  ************/
/*                                                                       */
/* mode 1: use framebuffer, in KPOC mode                                 */
/* mode 0: use surface flinger , in IPO mode                             */
/*                                                                       */
/***                                                                   ***/
#define DRAW_ANIM_MODE_FB       1
#define DRAW_ANIM_MODE_SURFACE  0

// static int HDISPLAY;
// static int VDISPLAY;
// static int FB_BPP;
// static int FB_WIDTH;
static Drm_resource drm_resource_object;
int get_int_value(const char * path);
int get_charging_type(const char* path);
int get_fast_charging_state();

int get_battery_temperature();
bool get_battery_online_status();
bool get_float_charging_state();
int get_charge_online_state();
bool get_bypass_setting();
/* public function*/

// common api
void anim_init();
void anim_deinit();

// set running mode function
void set_anim_version(int version);
void set_draw_mode(int draw_mode);

// show logos function
void show_boot_logo(void);
void show_kernel_logo(void);
void show_low_battery(void);
void show_charger_ov_logo(void);
void show_black_logo(void);
void show_battery_temperature_low(void);
void show_battery_temperature_high(void);
void show_charging_no_battery(void);
void show_charger_bad_contact(void);
void show_float_photo(void);

// show plug out logo
void show_plug_out_capacity(unsigned int capacity);

// show aniamtion function
void show_battery_capacity(unsigned int capacity);
// add this function for test.
void show_fast_charging(unsigned int capacity);
// check multibin status
bool get_logo_multibin_status(void);

//multi bin
    // partition related parameters
    #define MKIMG_NAME_SZ             (32)
    #define MKIMG_HDR_SZ              (0x200)
    #define ROUNDUP(a,b)      (((a) + ((b)-1)) & ~((b)-1))

    union part_hdr_t {
        struct {
            uint32_t magic;
            uint32_t dsz;
            char name[MKIMG_NAME_SZ];
            uint32_t maddr;
            uint32_t mode;
            /* extension */
            uint32_t ext_magic;
            uint32_t hdr_sz;
            uint32_t hdr_ver;
            uint32_t img_type;
            uint32_t img_list_end;
            uint32_t align_sz;
            uint32_t dsz_extend;
            uint32_t maddr_extend;
            uint32_t scrambled;
        } info;
        uint8_t data[MKIMG_HDR_SZ];
    };

/* internal function */
// set multibin status
void set_logo_multibin_status(bool bin_status);

// charging logo init and deinit
void anim_logo_init(void);
void anim_logo_deinit(void);

// framebuffer function

int  anim_fb_init(void);
void anim_fb_deinit(void);
void anim_fb_disp_update(void);
void anim_fb_addr_switch(void);

// surface flinger function
void anim_surface_init(void);
void anim_surface_deinit(void);

// show logo function
void anim_show_logo(int index);

// drm function
int anim_drm_pre_init(void);
void init_drm_addresses(void);
void free_drm_address(void);
int anim_drm_init(void);
void anim_drm_deinit(void);
void anim_drm_disp_update(void);
int is_drm(void);
void turn_off_drm_display(void);

//get ext lcd dimension api
int get_ext_lcd_width(void);
int get_ext_lcd_height(void);
void display_bypass_logo(void);

// libdrm API structure
typedef struct
{
    drmModeModeInfo DrmInfo;
    drmModeFBPtr DrmFb;
    drmModeCrtcPtr Drmcrtc;
} libDrmApi;

typedef struct
{
    int HDISPLAY;
    int VDISPLAY;
    int FB_BPP;
    int FB_WIDTH;
    int IS_DRM;
} chargingAnimationDimensionAPI;

#ifdef __cplusplus
}
#endif

#endif