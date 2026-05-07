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


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "show_animation_common.h"
#include "show_logo_log.h"

#include <platform/timer.h>
#include "decompress_common.h"
#include <mt_disp_drv.h>
#include "vitural_battery_logo.h"
#include "vitural_battery_word_cn.h"
#include "vitural_battery_word_en_lk.h"
extern LCM_SCREEN_T phical_screen;
#ifndef mdelay
#define mdelay(x)       spin((x) * 1000)
#endif


static int charging_low_index = 0;
static int charging_animation_index = 0;
static int version0_charging_index = 0;
static int bits = 0;
static int is_logo = 1;
static int logo_offset = -1;
static int logo_offset_ext = -1;
static void *g_dec_logo_addr = NULL;

#define CHECK_LOGO_BIN_OK  0
#define CHECK_LOGO_BIN_ERROR  -1
#define LOGO_OFFSET "logo-offset"
#define LOGO_OFFSET_EXT "logo-offset-ext"

static unsigned short  number_pic_addr[(NUMBER_RIGHT - NUMBER_LEFT)*(NUMBER_BOTTOM - NUMBER_TOP)*4] = {0x0}; //addr
static unsigned short  line_pic_addr[(TOP_ANIMATION_RIGHT - TOP_ANIMATION_LEFT)*4] = {0x0};
static unsigned short  percent_pic_addr[(PERCENT_RIGHT - PERCENT_LEFT)*(PERCENT_BOTTOM - PERCENT_TOP)*4] = {0x0};
static unsigned short  top_animation_addr[(TOP_ANIMATION_RIGHT - TOP_ANIMATION_LEFT)*(TOP_ANIMATION_BOTTOM - TOP_ANIMATION_TOP)*4] = {0x0};

/*
 * Check whether logo.bin is loaded or not. Avoid error in META mode
 * enable : logo.bin needs to be loaded or not
 */

void enable_logo(bool enable)
{
    is_logo = enable;
}

/* set logo offset to env buffer */
static void set_logo_offset(int offest, int lcm)
{
    char *env_buf = NULL;

    env_buf = (char *)malloc(sizeof(int));
    memset(env_buf, 0x00, sizeof(int));

    sprintf(env_buf, "%d", offest);

    if (lcm == LCD_MAIN)
        set_env(LOGO_OFFSET, env_buf);
    else
        set_env(LOGO_OFFSET_EXT, env_buf);

    free(env_buf);
    return;
}

/* get logo offset from env buffer */
static int get_logo_offset(int lcm)
{
    char *env_buf = NULL;
    int offset = -1;

    if (lcm == LCD_MAIN)
        env_buf = get_env(LOGO_OFFSET);
    else
        env_buf = get_env(LOGO_OFFSET_EXT);

    if (env_buf == NULL)
        return -1;

    offset =  atoi(env_buf);
    return offset;
}

/*
 * Check logo.bin address if valid, and get logo related info
 *
 */
int check_logo_index_valid(unsigned int index, void * logo_addr, LOGO_PARA_T * logo_info)
{
    if (!is_logo) {
        return CHECK_LOGO_BIN_ERROR;
    }
    unsigned int *pinfo = (unsigned int*)logo_addr;
    logo_info->logonum = pinfo[0];

    LOG_ANIM("[show_animation_common: %s %d]logonum =%d, index =%d\n", __FUNCTION__,__LINE__,logo_info->logonum, index);
    if (index >= logo_info->logonum)
    {
        LOG_ANIM("[show_animation_common: %s %d]unsupported logo, index =%d\n", __FUNCTION__,__LINE__, index);
        return CHECK_LOGO_BIN_ERROR;

    }

    if(index < logo_info->logonum - 1)
        logo_info->logolen = pinfo[3+index] - pinfo[2+index];
    else
        logo_info->logolen = pinfo[1] - pinfo[2+index];

    logo_info->inaddr = (unsigned int)logo_addr + pinfo[2+index];
    LOG_ANIM("show_animation_common, in_addr=0x%08x,  logolen=%d\n",
                logo_info->inaddr,  logo_info->logolen);

    return CHECK_LOGO_BIN_OK;
}

int get_total_logo_images_entries(void)
{
    int total_logo_entries = 0;

    total_logo_entries = LOGOS_COUNT_NORMAL;
#if defined(MTK_FAST_CHARGER_TECH)
    total_logo_entries += LOGOS_COUNT_FAST_CHARGING;
#endif
#if defined(MTK_WIRELESS_CHARGER_SUPPORT)
    total_logo_entries += LOGOS_COUNT_WIRELESS;
#endif
    return total_logo_entries;
}

int calculate_logo_offset(unsigned int index, void * dec_logo_addr,
                          void * logo_addr, LCM_SCREEN_T phical_screen)
{
    LOGO_PARA_T logo_info;
    int logo_width = phical_screen.width;
    int logo_height = phical_screen.height;
    int raw_data_size;
    unsigned int *pinfo;
    unsigned int logo_index = index;
    int total_logo_entries = 0;
    int *logo_offset_by_lcm = NULL;

    if (phical_screen.lcm == LCD_MAIN){
        logo_offset_by_lcm = &logo_offset;
    } else{
        logo_offset_by_lcm = &logo_offset_ext;
    }

    *logo_offset_by_lcm = get_logo_offset(phical_screen.lcm);
    LOG_ANIM("[get_logo_offset: %s %d]logo_offset = %d\n",__FUNCTION__,__LINE__,
            *logo_offset_by_lcm);

    if (*logo_offset_by_lcm != -1)
        return *logo_offset_by_lcm;

    pinfo = (unsigned int*)logo_addr;
    *logo_offset_by_lcm = 0;

    total_logo_entries = get_total_logo_images_entries();
    LOG_ANIM("[calculate_logo_offset: %s %d]pinfo[0] = %d\n",__FUNCTION__,__LINE__, pinfo[0]);
    LOG_ANIM("[calculate_logo_offset: %s %d]TOTAL_LOG0_COUNT = %d\n",__FUNCTION__,__LINE__,
          total_logo_entries);

    if(total_logo_entries == CHECK_LOGO_BIN_ERROR){
        *logo_offset_by_lcm = -1;
        return *logo_offset_by_lcm;
    }

    while(logo_index < pinfo[0]){
        if(check_logo_index_valid(logo_index, logo_addr, &logo_info) != CHECK_LOGO_BIN_OK){
            LOG_ANIM("[calculate_logo_offset: %s %d][Error]Logo resolution not correct",
                     __FUNCTION__,__LINE__);
            *logo_offset_by_lcm = -1;
            set_logo_offset(*logo_offset_by_lcm, phical_screen.lcm);
            return *logo_offset_by_lcm;
        }
        raw_data_size = decompress_logo((void*)logo_info.inaddr, dec_logo_addr, logo_info.logolen,
                                        phical_screen.fb_size);
        LOG_ANIM("[calculate_logo_offset: %s %d]Width = %d\n  Height = %d\n  raw_data_size = %d\n",
              __FUNCTION__,__LINE__, logo_width, logo_height, raw_data_size);

        if (raw_data_size == logo_width*logo_height*4) {
            bits = 32;
            break;
        } else if (raw_data_size == logo_width*logo_height*2) {
            bits = 16;
            break;
        } else {
            *logo_offset_by_lcm += total_logo_entries;// Add number of logos entries
            logo_index += total_logo_entries;
        }
        LOG_ANIM("[calculate_logo_offset: %s %d]bits = %d\n",__FUNCTION__,__LINE__, bits);
    }

    LOG_ANIM("[set_logo_offset: %s %d]logo_offset = %d\n",__FUNCTION__,__LINE__, *logo_offset_by_lcm);
    set_logo_offset(*logo_offset_by_lcm, phical_screen.lcm);
    return *logo_offset_by_lcm;
}

void display_hardcoded_logo(void *fill_addr)
{
    unsigned int *fb = (unsigned int *)fill_addr;
    if (!fb) {
        dprintf(CRITICAL, "[LOGO] Failed to get VRAM base\n");
        return;
    }
    printf("fb = %p\n", fb);
    unsigned int fb_stride = phical_screen.allignWidth;  // 关键：对齐后的行宽（像素数）
    unsigned int screen_w = phical_screen.width;
    unsigned int screen_h = phical_screen.height;

    int start_x = (screen_w - LOGO_WIDTH) / 2;
    int start_y = (screen_h - LOGO_HEIGHT) / 2;

    for (int y = 0; y < LOGO_HEIGHT; y++) {
        for (int x = 0; x < LOGO_WIDTH; x++) {
            // 源像素索引（RGBA 字节数组）
            int src_idx = (y * LOGO_WIDTH + x) * 4;
            unsigned char r = g_logo_argb[src_idx + 0];
            unsigned char g = g_logo_argb[src_idx + 1];
            unsigned char b = g_logo_argb[src_idx + 2];
            unsigned char a = g_logo_argb[src_idx + 3];

            // 组装成 ARGB 整数值（假设小端 CPU，整数值为 A<<24 | R<<16 | G<<8 | B）
            unsigned int argb_pixel = (a << 24) | (r << 16) | (g << 8) | b;

            // 目标像素索引（使用 stride）
            int dst_idx = (start_y + y) * fb_stride + (start_x + x);
            fb[dst_idx] = argb_pixel;  // 直接赋值 32 位整数
        }
    }
    display_hardcoded_logo_word(fb, screen_w, screen_h);
    mt_disp_update(0, 0, screen_w, screen_h);
    
    
    dprintf(INFO, "[LOGO] Hardcoded logo displayed\n");
}
void display_hardcoded_logo_word(unsigned int* fb, int screen_w, int screen_h)
{
    // 获取对齐后的行宽（像素数）
    unsigned int fb_stride = phical_screen.allignWidth;

    int width_word = LOGO_WIDTH_EN;
    int height_word = LOGO_HEIGHT_EN;

    // 计算文字图片的起始坐标（保持与原逻辑相同）
    int start_x_word = (screen_w - width_word) / 2 - (LOGO_WIDTH + width_word) / 2 - 70;
    int start_y_word = (screen_h - height_word) / 2;

    for (int y = 0; y < height_word; y++) {
        for (int x = 0; x < width_word; x++) {
            // 源像素索引（RGBA 字节数组）
            int src_idx = (y * width_word + x) * 4;
            unsigned char r = g_word_logo_argb_en[src_idx + 0];
            unsigned char g = g_word_logo_argb_en[src_idx + 1];
            unsigned char b = g_word_logo_argb_en[src_idx + 2];
            unsigned char a = g_word_logo_argb_en[src_idx + 3];

            // 转换为 ARGB 整数值（小端 CPU）
            unsigned int argb_pixel = (a << 24) | (r << 16) | (g << 8) | b;

            // 目标像素索引（使用 stride）
            int dst_x = start_x_word + x;
            int dst_y = start_y_word + y;
            if (dst_x >= 0 && dst_x < screen_w && dst_y >= 0 && dst_y < screen_h) {
                int dst_idx = dst_y * fb_stride + dst_x;
                fb[dst_idx] = argb_pixel;
            }
        }
    }
} 
void display_hardcoded_logo_word_cn(unsigned int* fb, int screen_w, int screen_h)
{
    unsigned int fb_stride = phical_screen.allignWidth;

    int width_word = LOGO_WIDTH_CN;
    int height_word = LOGO_HEIGHT_CN;

    int start_x_word = (screen_w - width_word) / 2 - (LOGO_WIDTH + width_word) / 2 - 70;
    int start_y_word = (screen_h - height_word) / 2;

    for (int y = 0; y < height_word; y++) {
        for (int x = 0; x < width_word; x++) {
            int src_idx = (y * width_word + x) * 4;
            unsigned char r = g_word_logo_argb[src_idx + 0];
            unsigned char g = g_word_logo_argb[src_idx + 1];
            unsigned char b = g_word_logo_argb[src_idx + 2];
            unsigned char a = g_word_logo_argb[src_idx + 3];
            unsigned int argb_pixel = (a << 24) | (r << 16) | (g << 8) | b;

            int dst_x = start_x_word + x;
            int dst_y = start_y_word + y;
            if (dst_x >= 0 && dst_x < screen_w && dst_y >= 0 && dst_y < screen_h) {
                int dst_idx = dst_y * fb_stride + dst_x;
                fb[dst_idx] = argb_pixel;
            }
        }
    }
}


/*
 * Fill a screen size buffer with logo content
 *
 */
void fill_animation_logo(unsigned int index, void *fill_addr, void * dec_logo_addr, void * logo_addr, LCM_SCREEN_T phical_screen)
{
    LOGO_PARA_T logo_info;
    int logo_width;
    int logo_height;
    int raw_data_size;
    int logo_index = index;
    int logo_offset = -1;
    g_dec_logo_addr = dec_logo_addr;
    logo_offset = calculate_logo_offset(index, dec_logo_addr, logo_addr, phical_screen);
    if(logo_offset == -1){
        return;
    }
    logo_index = logo_index + logo_offset;

    if(check_logo_index_valid(logo_index, logo_addr, &logo_info) != CHECK_LOGO_BIN_OK)
        return;

    raw_data_size = decompress_logo((void*)logo_info.inaddr, dec_logo_addr, logo_info.logolen, phical_screen.fb_size);

    logo_width = phical_screen.width;
    logo_height = phical_screen.height;
    if (phical_screen.rotation == 270 || phical_screen.rotation == 90) {
        logo_width = phical_screen.height;
        logo_height = phical_screen.width;
    }
    LOG_ANIM("[show_animation_common: %s %d]Width = %d\n  Height = %d\n  raw_data_size = %d\n",__FUNCTION__,__LINE__, logo_width, logo_height, raw_data_size);
    if (0 == bits) {
        if (raw_data_size == logo_width*logo_height*4) {
            bits = 32;
        } else if (raw_data_size == logo_width*logo_height*2) {
            bits = 16;
        } else {
            LOG_ANIM("[show_animation_common: %s %d]Logo data error\n",__FUNCTION__,__LINE__);
            return;
        }
        LOG_ANIM("[show_animation_common: %s %d]bits = %d\n",__FUNCTION__,__LINE__, bits);
    }

    RECT_REGION_T rect = {0, 0, logo_width, logo_height};

    fill_rect_with_content(fill_addr, rect, dec_logo_addr, phical_screen, bits);

}

/*
 * Fill a rectangle size address with special color
 *
 */
void fill_animation_prog_bar(RECT_REGION_T rect_bar,
                       unsigned int fgColor,
                       unsigned int start_div, unsigned int occupied_div,
                       void *fill_addr, LCM_SCREEN_T phical_screen)
{
    unsigned int div_size  = (rect_bar.bottom - rect_bar.top) / (ANIM_V0_REGIONS);
    unsigned int draw_size = div_size - (ANIM_V0_SPACE_AFTER_REGION);

    unsigned int i;

    for (i = start_div; i < start_div + occupied_div; ++ i)
    {
        unsigned int draw_bottom = rect_bar.bottom - div_size * i - (ANIM_V0_SPACE_AFTER_REGION);
        unsigned int draw_top    = draw_bottom - draw_size;

        RECT_REGION_T rect = {rect_bar.left, draw_top, rect_bar.right, draw_bottom};

        fill_rect_with_color(fill_addr, rect, fgColor, phical_screen);

    }
}


/*
 * Fill a rectangle with logo content
 *
 */
void fill_animation_dynamic(unsigned int index, RECT_REGION_T rect, void *fill_addr, void * dec_logo_addr, void * logo_addr, LCM_SCREEN_T phical_screen)
{
    LOGO_PARA_T logo_info;
    int raw_data_size;
    int logo_index = index;
    int logo_offset = 0;
    g_dec_logo_addr = dec_logo_addr;
    logo_offset = calculate_logo_offset(index, dec_logo_addr, logo_addr, phical_screen);
    if(logo_offset == -1){
        return;
    }
    logo_index = logo_index + logo_offset;
    if(check_logo_index_valid(logo_index, logo_addr, &logo_info) != CHECK_LOGO_BIN_OK)
        return;
    raw_data_size = decompress_logo((void*)logo_info.inaddr, (void*)dec_logo_addr, logo_info.logolen, (rect.right-rect.left)*(rect.bottom-rect.top)*4);

    if (0 == bits) {
        if (raw_data_size == (rect.right-rect.left)*(rect.bottom-rect.top)*4) {
            bits = 32;
        } else if (raw_data_size == (rect.right-rect.left)*(rect.bottom-rect.top)*2) {
            bits = 16;
        } else {
            LOG_ANIM("[show_animation_common: %s %d]Logo data error\n",__FUNCTION__,__LINE__);
            return;
        }
        LOG_ANIM("[show_animation_common: %s %d]bits = %d\n",__FUNCTION__,__LINE__, bits);
    }
    fill_rect_with_content(fill_addr, rect, dec_logo_addr, phical_screen, bits);
}


/*
 * Fill a rectangle  with number logo content
 *
 * number_position: 0~1st number, 1~2nd number
 */
void fill_animation_number(unsigned int index, unsigned int number_position, void *fill_addr,  void * logo_addr, LCM_SCREEN_T phical_screen)
{
    LOG_ANIM("[show_animation_common: %s %d]index= %d, number_position = %d\n",__FUNCTION__,__LINE__, index, number_position);

    LOGO_PARA_T logo_info;
    int raw_data_size;
    int logo_index = index;
    int logo_offset = 0;

    logo_offset = calculate_logo_offset(index, g_dec_logo_addr, logo_addr, phical_screen);
    if(logo_offset == -1){
        return;
    }
    logo_index = logo_index + logo_offset;
    if(check_logo_index_valid(logo_index, logo_addr, &logo_info) != CHECK_LOGO_BIN_OK)
        return;

    // draw default number rect,
    raw_data_size = decompress_logo((void*)logo_info.inaddr, (void*)number_pic_addr, logo_info.logolen, number_pic_size);

    //static RECT_REGION_T number_location_rect = {NUMBER_LEFT,NUMBER_TOP,NUMBER_RIGHT,NUMBER_BOTTOM};
    RECT_REGION_T battery_number_rect = {NUMBER_LEFT + (NUMBER_RIGHT - NUMBER_LEFT)*number_position,
                            NUMBER_TOP,
                            NUMBER_RIGHT + (NUMBER_RIGHT - NUMBER_LEFT)*number_position,
                            NUMBER_BOTTOM};

    if (0 == bits) {
        if (raw_data_size == (NUMBER_RIGHT - NUMBER_LEFT)*(NUMBER_BOTTOM - NUMBER_TOP)*4) {
            bits = 32;
        } else if (raw_data_size == (NUMBER_RIGHT - NUMBER_LEFT)*(NUMBER_BOTTOM - NUMBER_TOP)*2) {
            bits = 16;
        } else {
            LOG_ANIM("[show_animation_common: %s %d]Logo data error\n",__FUNCTION__,__LINE__);
            return;
        }
        LOG_ANIM("[show_animation_common: %s %d]bits = %d\n",__FUNCTION__,__LINE__, bits);
    }
    fill_rect_with_content(fill_addr, battery_number_rect, number_pic_addr,phical_screen, bits);
}

/*
 * Fill a line with special color
 *
 */
void fill_animation_line(unsigned int index, unsigned int capacity_grids, void *fill_addr,  void * logo_addr, LCM_SCREEN_T phical_screen)
{
    LOGO_PARA_T logo_info;
    int raw_data_size;
    int logo_index = index;
    int logo_offset = 0;
    logo_offset = calculate_logo_offset(index, g_dec_logo_addr, logo_addr, phical_screen);
    if(logo_offset == -1){
        return;
    }
    logo_index = logo_index + logo_offset;
    if(check_logo_index_valid(logo_index, logo_addr, &logo_info) != CHECK_LOGO_BIN_OK)
        return;

    raw_data_size = decompress_logo((void*)logo_info.inaddr, (void*)line_pic_addr, logo_info.logolen, line_pic_size);

    if (0 == bits) {
        if (raw_data_size == (TOP_ANIMATION_RIGHT - TOP_ANIMATION_LEFT)*4) {
            bits = 32;
        } else if (raw_data_size == (TOP_ANIMATION_RIGHT - TOP_ANIMATION_LEFT)*2) {
            bits = 16;
        } else {
            LOG_ANIM("[show_animation_common: %s %d]Logo data error\n",__FUNCTION__,__LINE__);
            return;
        }
        LOG_ANIM("[show_animation_common: %s %d]bits = %d\n",__FUNCTION__,__LINE__, bits);
    }
    RECT_REGION_T rect = {CAPACITY_LEFT, CAPACITY_TOP, CAPACITY_RIGHT, CAPACITY_BOTTOM};
    int i = capacity_grids;
    for(; i < CAPACITY_BOTTOM; i++)
    {
        rect.top = i;
        rect.bottom = i+1;
        fill_rect_with_content(fill_addr, rect, line_pic_addr, phical_screen, bits);

    }
}

/*
 * Show old charging animation
 *
 */
void fill_animation_battery_old(unsigned int capacity,  void *fill_addr, void * dec_logo_addr, void * logo_addr,
                       LCM_SCREEN_T phical_screen)
{
    int capacity_grids = 0;
    if (capacity > 100) capacity = 100;
    capacity_grids = (capacity * (ANIM_V0_REGIONS)) / 100;

    if (version0_charging_index < capacity_grids * 2)
        version0_charging_index = capacity_grids * 2;

    if (capacity < 100){
        version0_charging_index > 7? version0_charging_index = capacity_grids * 2 : version0_charging_index++;
    } else {
        version0_charging_index = ANIM_V0_REGIONS * 2;
    }

    fill_animation_logo(ANIM_V0_BACKGROUND_INDEX, fill_addr, dec_logo_addr, logo_addr,phical_screen);

    RECT_REGION_T rect_bar = {bar_rect.left + 1, bar_rect.top + 1, bar_rect.right, bar_rect.bottom};

    fill_animation_prog_bar(rect_bar,
                       (unsigned int)(BAR_OCCUPIED_COLOR), 
                       0,  version0_charging_index/2,
                       fill_addr, phical_screen);                              

    fill_animation_prog_bar(rect_bar,
                      (unsigned int)(BAR_EMPTY_COLOR),
                      version0_charging_index/2, ANIM_V0_REGIONS - version0_charging_index/2,
                      fill_addr, phical_screen); 

}

/*
 * Show new charging animation
 *
 */
void fill_animation_battery_new(unsigned int capacity, void *fill_addr, void * dec_logo_addr, void * logo_addr, LCM_SCREEN_T phical_screen)
{
    LOG_ANIM("[show_animation_common: %s %d]capacity : %d\n",__FUNCTION__,__LINE__, capacity);

    if (capacity >= 100) {
        //show_logo(37); // battery 100
        fill_animation_logo(FULL_BATTERY_INDEX, fill_addr, dec_logo_addr, logo_addr,phical_screen);

    } else if (capacity < 10) {
        LOG_ANIM("[show_animation_common: %s %d]charging_low_index = %d\n",__FUNCTION__,__LINE__, charging_low_index);
        charging_low_index ++ ;

        fill_animation_logo(LOW_BAT_ANIM_START_0 + charging_low_index, fill_addr, dec_logo_addr, logo_addr,phical_screen);
        fill_animation_number(NUMBER_PIC_START_0 + capacity, 1, fill_addr, logo_addr, phical_screen);
        fill_animation_dynamic(NUMBER_PIC_PERCENT, percent_location_rect, fill_addr, percent_pic_addr, logo_addr, phical_screen);

        if (charging_low_index >= 9) charging_low_index = 0;

    } else {

        unsigned int capacity_grids = 0;
        //static RECT_REGION_T battery_rect = {CAPACITY_LEFT,CAPACITY_TOP,CAPACITY_RIGHT,CAPACITY_BOTTOM};
        capacity_grids = CAPACITY_BOTTOM - (CAPACITY_BOTTOM - CAPACITY_TOP) * (capacity - 10) / 90;
        LOG_ANIM("[show_animation_common: %s %d]capacity_grids : %d,charging_animation_index = %d\n",__FUNCTION__,__LINE__, capacity_grids,charging_animation_index);

        //background
        fill_animation_logo(ANIM_V1_BACKGROUND_INDEX, fill_addr, dec_logo_addr, logo_addr,phical_screen);

        fill_animation_line(ANIM_LINE_INDEX, capacity_grids, fill_addr,  logo_addr, phical_screen);
        fill_animation_number(NUMBER_PIC_START_0 + (capacity/10), 0, fill_addr, logo_addr, phical_screen);
        fill_animation_number(NUMBER_PIC_START_0 + (capacity%10), 1, fill_addr, logo_addr, phical_screen);
        fill_animation_dynamic(NUMBER_PIC_PERCENT, percent_location_rect, fill_addr, percent_pic_addr, logo_addr, phical_screen);


         if (capacity <= 90)
         {
            RECT_REGION_T top_animation_rect = {TOP_ANIMATION_LEFT, capacity_grids - (TOP_ANIMATION_BOTTOM - TOP_ANIMATION_TOP), TOP_ANIMATION_RIGHT, capacity_grids};
            //top_animation_rect.bottom = capacity_grids;
            //top_animation_rect.top = capacity_grids - top_animation_height;
            charging_animation_index++;
            //show_animation_dynamic(15 + charging_animation_index, top_animation_rect, top_animation_addr);
            fill_animation_dynamic(BAT_ANIM_START_0 + charging_animation_index, top_animation_rect, fill_addr, top_animation_addr, logo_addr, phical_screen);

            if (charging_animation_index >= 9) charging_animation_index = 0;
         }
    }

}

/*
 * Show wireless charging animation
 * total 29 logo:from 39 ~ 68
 * less(0<10): 50-53 , low(<30):54-57 ,middle(<60):58-61 , high():62-75 , o:66, full:67,num (0-9):39-48, %:49
 *
 */
 void fill_animation_battery_wireless_charging(unsigned int capacity, void *fill_addr, void * dec_logo_addr, void * logo_addr, LCM_SCREEN_T phical_screen)
{
    LOG_ANIM("[show_animation_common: %s %d]capacity : %d\n",__FUNCTION__,__LINE__, capacity);
//    RECT_REGION_T wireless_bgd_rect = {0, 0, phical_screen.width, phical_screen.height};

    charging_low_index >= 3? charging_low_index = 0:charging_low_index++;
    LOG_ANIM("[show_animation_common: %s %d]charging_low_index = %d\n",__FUNCTION__,__LINE__, charging_low_index);

    if (capacity >= 100) {
         // battery 100
        fill_animation_logo(V2_BAT_100_INDEX, fill_addr, dec_logo_addr, logo_addr,phical_screen);
    } else if (capacity <= 0) {
        fill_animation_logo(V2_BAT_0_INDEX, fill_addr, dec_logo_addr, logo_addr,phical_screen);
    } else {
        int bg_index = V2_BAT_0_10_START_INDEX; //capacity > 0 && capacity < 10
        if (capacity >= 10 && capacity < 40) {
            bg_index = V2_BAT_10_40_START_INDEX;
        } else if (capacity >= 40 && capacity < 80) {
            bg_index = V2_BAT_40_80_START_INDEX;
        } else if (capacity >= 80 && capacity < 100) {
            bg_index = V2_BAT_80_100_START_NDEX;
        }
        fill_animation_logo(bg_index + charging_low_index, fill_addr, dec_logo_addr, logo_addr,phical_screen);
        RECT_REGION_T tmp_rect = {(int)phical_screen.width * 4/10,
                        (int) phical_screen.height * 1/6,
                        (int)phical_screen.width* 5/10,
                        (int)phical_screen.height*16/60};
        unsigned short tmp_num_addr[(int)phical_screen.width * phical_screen.height/100]; //addr

        if (capacity >= 10) {
            LOG_ANIM("[show_animation_common: %s %d]tmp_rect left = %d, right = %d,top = %d,bottom = %d,\n",__FUNCTION__,__LINE__,
                        tmp_rect.left,tmp_rect.right,tmp_rect.top,tmp_rect.bottom);
            fill_animation_dynamic(V2_NUM_START_0_INDEX + (capacity/10), tmp_rect, fill_addr, tmp_num_addr, logo_addr, phical_screen);
            tmp_rect.left += (int)phical_screen.width /10;
            tmp_rect.right += (int)phical_screen.width /10;
        }

        LOG_ANIM("[show_animation_common: %s %d]tmp_rect left = %d, right = %d,top = %d,bottom = %d,\n",__FUNCTION__,__LINE__,
                tmp_rect.left,tmp_rect.right,tmp_rect.top,tmp_rect.bottom);
        fill_animation_dynamic(V2_NUM_START_0_INDEX + (capacity%10), tmp_rect, fill_addr, tmp_num_addr, logo_addr, phical_screen);

        tmp_rect.left += (int)phical_screen.width /10;
        tmp_rect.right += (int)phical_screen.width /10;

        LOG_ANIM("[show_animation_common: %s %d]tmp_rect left = %d, right = %d,top = %d,bottom = %d,\n",__FUNCTION__,__LINE__,
                        tmp_rect.left,tmp_rect.right,tmp_rect.top,tmp_rect.bottom);
        fill_animation_dynamic(V2_NUM_PERCENT_INDEX, tmp_rect, fill_addr, tmp_num_addr, logo_addr, phical_screen);

    }
}

/*
 * Show charging animation by version
 *
 */
void fill_animation_battery_by_ver(unsigned int capacity,void *fill_addr, void * dec_logo_addr, void * logo_addr,
                        LCM_SCREEN_T phical_screen, int version)
{
    LOG_ANIM("[show_animation_common: %s %d]version : %d\n",__FUNCTION__,__LINE__, version);
    switch (version)
    {
        case VERION_OLD_ANIMATION:
            fill_animation_battery_old(capacity, fill_addr, dec_logo_addr, logo_addr, phical_screen);

            break;
        case VERION_NEW_ANIMATION:
            fill_animation_battery_new(capacity, fill_addr, dec_logo_addr, logo_addr, phical_screen);

            break;
        case VERION_WIRELESS_CHARGING_ANIMATION:
            fill_animation_battery_wireless_charging(capacity, fill_addr, dec_logo_addr, logo_addr, phical_screen);

            break;
        default:
            fill_animation_battery_old(capacity, fill_addr, dec_logo_addr, logo_addr, phical_screen);

            break;
    }
}