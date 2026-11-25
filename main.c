/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Luban-Lite application entry for rt-carplay.
 *
 * Initialize the application/rt-thread/rt-carplay-demo submodule and select
 * the matching hardware defconfig. Without RTCARPLAY_AUTO_START the receiver
 * waits for the carplay_start MSH command, which is the recommended order
 * for first bring-up (the console then shows the pairing PIN).
 */
#include <rtthread.h>

#include "carplay_app.h"
#if defined(RTCARPLAY_AUTO_CONNECT)
#include "connection_manager.h"
#endif
#if defined(RTCARPLAY_OTA) && !defined(RTCARPLAY_DEMO_LAUNCHER)
#include "ota_lubanlite.h"
#endif
#include "rtcp/rtcp.h"

#ifdef RTCARPLAY_AUTO_START
static void rtcarplay_autostart(void)
{
    carplay_app_config_t config;
    carplay_app_config_defaults(&config);
    config.width_pixels = RTCARPLAY_SCREEN_WIDTH;
    config.height_pixels = RTCARPLAY_SCREEN_HEIGHT;
    config.rotation_degrees = RTCARPLAY_ROTATION;

    int result = carplay_app_start(&config);
    if (result != RTCP_OK)
    {
        rt_kprintf("rt-carplay autostart failed: %d\n", result);
    }
}
#endif

int main(void)
{
    rt_kprintf("Based on LIVI by Lasse Heitgres (https://github.com/f-io/LIVI)\n");
#if defined(RTCARPLAY_DEMO_LAUNCHER)
    rt_kprintf("rt-carplay: Launcher V2; automatic USB and wireless discovery\n");
#elif defined(RTCARPLAY_AUTO_CONNECT)
    int result;
    do
    {
        result = carplay_connection_manager_start();
        if (result != RTCP_OK)
        {
            rt_kprintf("rt-carplay: connection owner startup failed=%d; retrying\n", result);
            rt_thread_mdelay(1000);
        }
    } while (result != RTCP_OK);
#if defined(RTCARPLAY_OTA)
    carplay_ota_boot_health_ready();
#endif
    rt_kprintf("rt-carplay: automatic Headless USB/wireless discovery\n");
#elif defined(RTCARPLAY_AUTO_START)
    rtcarplay_autostart();
#else
    rt_kprintf("rt-carplay: run carplay_start, then carplay_status "
               "(PIN is printed at start)\n");
#endif
    return 0;
}
