/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#define RT_DEVICE_CTRL_RTC_GET_TIME 0x10
typedef void *rt_device_t;
rt_device_t rt_device_find(const char *name);
int rt_device_control(rt_device_t device, int command, void *argument);
