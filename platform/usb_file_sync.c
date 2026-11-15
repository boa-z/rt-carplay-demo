/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <rtthread.h>
#include <aic_core.h>

/* The descriptors and transfer service are supplied by CherryUSB. The device
 * bus index is independent of the USB host bus index used by the iPhone. */
extern void cherryadb_init(uint8_t busid, uint32_t reg_base);

static int carplay_usb_file_sync_init(void)
{
    cherryadb_init(0, USB_DEV_BASE);
    rt_kprintf("[usb-files] USB0 CherryUSB ADB sync (development FFFF:FFFF); "
               "push/pull only, USB1 iPhone host\n");
    return 0;
}
INIT_APP_EXPORT(carplay_usb_file_sync_init);
