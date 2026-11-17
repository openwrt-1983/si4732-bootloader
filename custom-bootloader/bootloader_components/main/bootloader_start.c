/*
 * SPDX-FileCopyrightText: 2015-2021 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdbool.h>
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "soc/reset_reasons.h"
#include "bootloader_init.h"
#include "bootloader_utility.h"
#include "bootloader_common.h"

static const char* TAG = "boot";

static int select_partition_number(bootloader_state_t *bs);

/*
 * We arrive here after the ROM bootloader finished loading this second stage bootloader from flash.
 * The hardware is mostly uninitialized, flash cache is down and the app CPU is in reset.
 * We do have a stack, so we can do the initialization in C.
 */
void __attribute__((noreturn)) call_start_cpu0(void)
{
    // 1. Hardware initialization
    if (bootloader_init() != ESP_OK) {
        bootloader_reset();
    }

#ifdef CONFIG_BOOTLOADER_SKIP_VALIDATE_IN_DEEP_SLEEP
    // If this boot is a wake up from the deep sleep then go to the short way,
    // try to load the application which worked before deep sleep.
    // It skips a lot of checks due to it was done before (while first boot).
    bootloader_utility_load_boot_image_from_deep_sleep();
    // If it is not successful try to load an application as usual.
#endif

    // 2. Select the number of boot partition
    bootloader_state_t bs = {0};
    int boot_index = select_partition_number(&bs);
    if (boot_index == INVALID_INDEX) {
        bootloader_reset();
    }

    // 2.1 Print a custom message!
    esp_rom_printf("[%s] %s\n", TAG, CONFIG_EXAMPLE_BOOTLOADER_WELCOME_MESSAGE);

    // 3. Load the app image for booting
    bootloader_utility_load_boot_image(&bs, boot_index);
}

// Select the number of boot partition
static int select_partition_number(bootloader_state_t *bs)
{
    // 1. Load partition table
    if (!bootloader_utility_load_partition_table(bs)) {
        ESP_LOGE(TAG, "load partition table error!");
        return INVALID_INDEX;
    }

    // 2. Custom rule:
    //    COLD boot (power on / brownout / watchdog / EN button) -> always
    //      start the factory app (our boot selector menu), ignoring otadata.
    //    Everything else follows otadata and starts the chosen OTA slot:
    //      - esp_restart() from the menu: CORE_SW (0x03) / CPU0_SW (0x0C)
    //      - wake from deep sleep: CORE_DEEP_SLEEP (0x05)
    //        (zhang-chong long-press sleep; wake must resume the same
    //         firmware instead of bouncing back to the boot menu)
    soc_reset_reason_t rst = (soc_reset_reason_t)esp_rom_get_reset_reason(0);
    bool follow_otadata = (rst == RESET_REASON_CORE_SW) ||
                          (rst == RESET_REASON_CPU0_SW) ||
                          (rst == RESET_REASON_CORE_DEEP_SLEEP);

    if (!follow_otadata) {
        if (bs->factory.offset != 0 && bs->factory.size != 0) {
            esp_rom_printf("[%s] cold boot (reason=%d) -> factory boot menu\n", TAG, (int)rst);
            return FACTORY_INDEX;
        }
    }

    // 3. Otherwise follow otadata normally
    return bootloader_utility_get_selected_boot_partition(bs);
}

// Return global reent struct if any newlib functions are linked to bootloader
struct _reent *__getreent(void)
{
    return _GLOBAL_REENT;
}
