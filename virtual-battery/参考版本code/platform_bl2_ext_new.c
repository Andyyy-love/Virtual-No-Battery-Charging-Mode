/*
 * Copyright (c) 2022 MediaTek Inc.
 *
 * Use of this source code is governed by a MIT-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/MIT
 */
#include <arch/arch_ops.h>
#include <arch/cs.h>
#include <assert.h>
#include <dconfig.h>
#include <dev/interrupt/arm_gic.h>
#include <dev/timer/arm_generic.h>
#include <dev/uart.h>
#include <dynamic_log.h>
#include <gpio_api.h>
#include <kernel/vm.h>
#include <lib/pl_boottags.h>
#include <lib/watchdog.h>
#include <lk/bits.h>
#include <lk/debug.h>
#include <lk/err.h>
#include <lk/reg.h>
#include <lk/trace.h>
#include <mblock.h>
#include <mmc_core.h>
#include <mminfra.h>
#include <mt_disp_drv.h>
#include <mt_i2c.h>
#include <spi_slave.h>
#include <mt_logo.h>
#ifdef MTK_MT6377_SUPPORT
#include <mt6315.h>
#else
#include <mt6368.h>
#endif
#include <mtk_battery.h>
#include <mtk_charger.h>
#include <mtk_dvfs.h>
#include <platform.h>
#include <platform/addressmap.h>
#include <platform/boot_mode.h>
#include <platform/irq.h>
#include <platform/leds.h>
#include <platform/memory_layout.h>
#include <platform/timer.h>
#include <platform_mtk.h>
#include <pmic_dlpt.h>
#include <profiling.h>
#include <sboot.h>
#include <spmi_common.h>
#include <string.h>
#include <sysenv.h>
#include <ufs_interface.h>
#include <ufs_platform.h>
#include <verified_boot.h>
#include <stdio.h>
#include <lib/bio.h>
#include <stdbool.h>
#include "disp/ddp_disp_bdg.h"
/*BEGIN csdk by kangwei5 2023/12/06 OCALLA-394*/
#include <lib/commercial.h>
/*END OCALLA-394*/
#define BOOT_TAG_BOOT_TIME       0x88610007
static struct boot_tag_boot_time {
    lk_time_t bl2_boot_time;
    lk_time_t bl2_ext_boot_time;
    lk_time_t logo_time;
    lk_time_t tfa_boot_time;
    lk_time_t sec_os_boot_time;
    lk_time_t gz_boot_time;
} time_profile;
PL_BOOTTAGS_TO_BE_UPDATED(time_profile, BOOT_TAG_BOOT_TIME, &time_profile);

/* Sycamore code for SYCAMORE-177 by kangkai4 at 20241127 start */
#define CODE_BUF_LEN           1024
#define NV_CONTRYCODE_OFFSET   60
#define NV_CONTRYCODE_LEN      4
#define PRO_INFO_NAME "proinfo"
#define COUNTRY_CODE_LEN    4
#define COUNTRY_CODE_CUST_OFFSET   2
#define CCODE_CUST_IS_DEMO_MODE(f) (((f) == 'A')||((f) == 'B')||((f) == 'F')||((f) == 'G')||\
    ((f) == 'H')||((f) == 'J')||((f) == 'K')||((f) == 'L'))
/* Sycamore code for SYCAMORE-177 by kangkai4 at 20241127 end */
#define NV_BYPASSENABLE_OFFSET 35
#define NV_BYPASSENABLE_LEN    1
#define BYPASSENABLE_LEN       1
static lk_time_t start_time;

#ifndef mdelay
#define mdelay(x)       spin((x) * 1000)
#endif

struct mmu_initial_mapping mmu_initial_mappings[] = {
    {
        .phys = MEMBASE,
        .virt = KERNEL_BASE,
        .size = MEMSIZE,
        .flags = 0,
        .name = "ram"
    },
    {
        .phys = HWVER_BASE_PHY,
        .virt = HWVER_BASE,
        .size = HWVER_SIZE,
        .flags = MMU_INITIAL_MAPPING_FLAG_DEVICE,
        .name = "hwver"
    },
    {
        .phys = GIC_BASE_PHY,
        .virt = GIC_BASE,
        .size = GIC_SIZE,
        .flags = MMU_INITIAL_MAPPING_FLAG_DEVICE,
        .name = "gic"
    },
    {
        .phys = MCUCFG_BASE_PHY,
        .virt = MCUCFG_BASE,
        .size = MCUCFG_SIZE,
        .flags = MMU_INITIAL_MAPPING_FLAG_DEVICE,
        .name = "mcucfg"
    },
    {
        .phys = IO_BASE_PHY,
        .virt = IO_BASE,
        .size = IO_SIZE,
        .flags = MMU_INITIAL_MAPPING_FLAG_DEVICE,
        .name = "io"
    },
    {
        .phys = PL_BOOTTAGS_BASE_PHY,
        .virt = PL_BOOTTAGS_BASE,
        .size = PL_BOOTTAGS_SIZE,
        .flags = 0,
        .name = "pl_boottags"
    },
    {
        .phys = SRAM_BASE_PHY,
        .virt = SRAM_BASE,
        .size = SRAM_SIZE,
        .flags = 0,
        .name = "sram"
    },
    /* null entry to terminate the list */
    { .size = 0 }
};

static pmm_arena_t arena = {
    .name = "dram",
    .base = MEMBASE,
    .size = MEMSIZE,
    .flags = PMM_ARENA_FLAG_KMAP,
};

#if ARCH_ARM64
void platform_elX_init(void)
{
    unsigned int current_el = ARM64_READ_SYSREG(CURRENTEL) >> 2;
    uint64_t vec_base_phy = (uint64_t)&arm64_el2_or_el3_exception_base;

    if (current_el == 3) {
#if WITH_KERNEL_VM
        vec_base_phy &= ~(~0ULL << MMU_KERNEL_SIZE_SHIFT);
#endif
        ARM64_WRITE_SYSREG(VBAR_EL3, vec_base_phy);
        ARM64_WRITE_SYSREG(cntfrq_el0, 13000000LL);
        gic_el3_setup();
        disp_el3_setup();
        disable_cache_share();
    }
}
#endif
/* Sycamore code for SYCAMORE-177 by kangkai4 at 20241127 start */
static int  readCountryCode(u8 *ccode, int l)
{
    int len = 0;
    u8 code[CODE_BUF_LEN + 1] = {0};
    bdev_t *bdev = NULL;

    if (l < NV_CONTRYCODE_LEN || !ccode) {
        return -1;
    }

    bdev = bio_open_by_label(PRO_INFO_NAME);
    if (NULL == bdev) {
        return -1;
    }

    len = bio_read(bdev, (u8 *)&code, NV_CONTRYCODE_OFFSET, NV_CONTRYCODE_LEN);
    if (len < NV_CONTRYCODE_LEN) {
           dprintf(CRITICAL, "read countrycode from Proinfo , failed to read %s (%d)\n", PRO_INFO_NAME ,len);
           bio_close (bdev);
           return -2;
    }

    bio_close (bdev);

    memcpy(ccode, code, NV_CONTRYCODE_LEN);
    dprintf(CRITICAL, "read countrycode is %s\n", code);

    return 0;
}

static bool readBypassEnable(bool *bypass_en)
{
    int len = 0;
    u8 code = 0;
    bdev_t *bdev = NULL;

    if (!bypass_en) {
        dprintf(CRITICAL, "[%s]: cannot get bypass_en\n",__func__);
        return false;
    }

    bdev = bio_open_by_label(PRO_INFO_NAME);
    if (NULL == bdev) {
        dprintf(CRITICAL, "[%s]: GET BIO LABEL FAILED\n",__func__);
        return false;
    }

    len = bio_read(bdev, (u8 *)&code, NV_BYPASSENABLE_OFFSET, NV_BYPASSENABLE_LEN);
    if (len < NV_BYPASSENABLE_LEN) {
        dprintf(CRITICAL, "read bypass_enable from Proinfo , failed to read %s (%d)\n", PRO_INFO_NAME ,len);
        bio_close (bdev);
        return false;
    }

    bio_close(bdev);

    *bypass_en = (code !=0) ? true : false;

    return true;
}

static int getCountryCode(u8 *ccode, int len)
{
    return readCountryCode(ccode, len);
}

static bool getBypassSetting(bool *ccode)
{
    bool ret = 0;
    ret = readBypassEnable(ccode);
    dprintf(CRITICAL, "readBypassEnable return = %d\n", (int)ret);
    return ret;
}

static bool isDeivceInDemoMode()
{
    static u8  ccode[COUNTRY_CODE_LEN] = {0};

    // read nv first time
    if (!ccode[0]) {
        int ret = getCountryCode(ccode, COUNTRY_CODE_LEN);
        if (ret < 0) {
            return false;
        }
    }

    if (CCODE_CUST_IS_DEMO_MODE(ccode[COUNTRY_CODE_CUST_OFFSET])) {
        return true;
    } else {
        return false;
    }

}
/* Sycamore code for SYCAMORE-177 by kangkai4 at 20241127 end */

void platform_init(void)
{
    bool res = false;
    bool bypass_en = false;
    void *fdt = NULL;
    /*BEGIN csdk by kangwei5 2023/12/06 OCALLA-394*/
    struct commercial_config csdk_cfg;
    /*END OCALLA-394*/
    /* do storage init above all */
#ifdef PROJECT_TYPE_FPGA
    int ret;

    PROFILING(ret = ufs_init());
    if (ret)
        PROFILING(emmc_init());
#else
    if ((readl(GPIO_BANK_BASE) & TRAP_UFS_FIRST) == 0)
        PROFILING(ufs_init());
    else
        PROFILING(emmc_init());
#endif

    /* do verify boot init */
    PROFILING(sec_lk_vb_init());

    /* do dconfig init */
    PROFILING(dconfig_initenv());
    PROFILING(env_init());

    /* do platform dtb init */
    PROFILING(fdt = platform_load_device_tree());

    /* do subpmic init */
#ifdef MTK_MT6377_SUPPORT
    PROFILING(mt6315_device_register(SPMI_MASTER_1, SPMI_SLAVE_3));
#else
    PROFILING(mt6368_device_register());
#endif
    /* do platform driver init */
    PROFILING(mt_gpio_set_default(fdt));
    PROFILING(i2c_hw_init(fdt));
#ifdef MTK_BDG_DSC
    /*open 6382 26m clk*/
    PROFILING(clk_buf_disp_ctrl(true, fdt));
    /* spi slave driver probe */
    PROFILING(spi_slave_probe(fdt));
#endif
    PROFILING(disp_init(fdt));
    PROFILING(leds_init(fdt));
    PROFILING(charger_init(fdt));
    PROFILING(mtk_battery_init(fdt));
    PROFILING(check_sw_ocv());
    PROFILING(mtk_charger_start());
    PROFILING(calc_dlpt_imix_r());
    PROFILING(leds_on());

    /* do cpu dvfs init */
    PROFILING(dvfs_init(fdt));
    if (mtk_charger_kpoc_check()) {
        /* Sycamore code for SYCAMORE-177 by kangkai4 at 20241127 start */
        if (isDeivceInDemoMode() == false) {
            /*BEGIN csdk by kangwei5 2023/12/06 OCALLA-394*/
            read_csdk_cfg_mmc(&csdk_cfg);
            if (csdk_cfg.charger_dis) {
				PROFILING(mt_disp_show_boot_logo());
            } else {
				PROFILING(mt_disp_show_low_battery());
            }
			/*END OCALLA-394*/
        } else
            PROFILING(mt_disp_show_boot_logo());
        /* Sycamore code for SYCAMORE-177 by kangkai4 at 20241127 end */
    } else if (platform_get_boot_mode() == NORMAL_BOOT ||
                platform_get_boot_mode() == ALARM_BOOT) {
        res = getBypassSetting(&bypass_en);
        if(!res){
            dprintf(CRITICAL, "platform_init: get bypass_en failed\n");
            PROFILING(mt_disp_show_boot_logo());
            time_profile.logo_time = current_time() - start_time;
            goto end;
        }
        if (bypass_en && get_chr_volt() < 3000) {
            dprintf(CRITICAL, "platform_init : bypass_en = %d, exec bypass_setting logic\n", bypass_en);
            PROFILING(mt_disp_show_vitural_battery());
            mdelay(4000);
            time_profile.logo_time = current_time() - start_time;
            chr_power_off();
        } else {
            dprintf(CRITICAL, "platform_init : bypass_en = %d, exec boot logic\n", bypass_en);
            PROFILING(mt_disp_show_boot_logo());
            time_profile.logo_time = current_time() - start_time;
        }
    }
    /* finished platform init, free dtb */
end:
    if (fdt)
        free(fdt);
}

void platform_early_init(void)
{
    arch_enable_serror();
    pmm_add_arena(&arena);
    uart_init_port(log_port(), log_baudrate());
    arm_gic_init();
    arm_generic_timer_init(ARM_GENERIC_TIMER_PHYSICAL_INT, 0);
    /* start time profiling after timer_init to get current_time correctly */
    start_time = current_time();
    platform_watchdog_set_enabled(true);
}

void platform_quiesce(void)
{
    int ret = 0;

    ret = sec_otp_ver_update_adapter(platform_get_boot_mode());
    if (ret)
        dprintf(CRITICAL, "[SEC] Failed to update OTP version, ret = 0x%x\n", ret);

#ifndef PROJECT_TYPE_FPGA
    if ((readl(GPIO_BANK_BASE) & TRAP_UFS_FIRST) == 0)
        ufs_deinit();
#endif

    /* Free the memory space of BL2 extension */
    mblock_free(MEMBASE);

    /* Finalize the boot time of BL2 extension before stopping timer
     * and updating pl_boottags.
     */
    time_profile.bl2_ext_boot_time = current_time() - start_time;
    platform_stop_timer();

    /* Buffer allocation for security use */
    sec_alloc_fw_protect_share_mem(PAGE_SIZE);

    /* Update pl_boottags registered with PL_BOOTTAGS_TO_BE_UPDATED. */
    update_pl_boottags((struct boot_tag *)PL_BOOTTAGS_BASE);
}

void platform_quiesce_el3(void)
{
    unsigned int current_el = ARM64_READ_SYSREG(CURRENTEL) >> 2;

    /* de-init gic, clean up for later TFA stage */
    if (current_el == 3)
        gic_el3_deinit();
}

static void pl_boottags_boot_time_hook(struct boot_tag *tag)
{
    struct boot_tag_boot_time *p = (struct boot_tag_boot_time *)&tag->data;

    time_profile.bl2_boot_time     = p->bl2_boot_time;
    time_profile.bl2_ext_boot_time = p->bl2_ext_boot_time;
    time_profile.logo_time         = p->logo_time;
    time_profile.tfa_boot_time     = p->tfa_boot_time;
    time_profile.sec_os_boot_time  = p->sec_os_boot_time;
    time_profile.gz_boot_time      = p->gz_boot_time;
}
PL_BOOTTAGS_INIT_HOOK(time_profile, BOOT_TAG_BOOT_TIME, pl_boottags_boot_time_hook);