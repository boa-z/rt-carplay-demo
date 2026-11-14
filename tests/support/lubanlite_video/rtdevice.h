/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
/* RT-Thread's rtdevice.h does not import Linux libc ioctl definitions.
 * The real MPP/board headers supply the SDK command encoding. */
typedef void *rt_device_t;
rt_device_t rt_device_find(const char *);
int rt_device_control(rt_device_t, int, void *);
