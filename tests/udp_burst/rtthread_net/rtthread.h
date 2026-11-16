/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
struct rt_device;
typedef struct rt_device *rt_device_t;
typedef int rt_err_t;
typedef long rt_base_t;
struct rt_mutex
{
    int held;
};
struct rt_device
{
    void *user_data;
    int (*control)(rt_device_t, int, void *);
};
#define RT_EOK             0
#define RT_EINVAL          22
#define RT_ERROR           1
#define RT_TRUE            1
#define RT_FALSE           0
#define RT_IPC_FLAG_PRIO   1
#define RT_WAITING_FOREVER (-1)
#define RT_WAITING_NO      0
#define RT_TICK_PER_SECOND 1000
unsigned rt_tick_get(void);
rt_base_t rt_hw_interrupt_disable(void);
void rt_hw_interrupt_enable(rt_base_t);
int rt_mutex_take(struct rt_mutex *, int);
int rt_mutex_release(struct rt_mutex *);
int rt_mutex_init(struct rt_mutex *, const char *, int);
int rt_mutex_detach(struct rt_mutex *);
rt_device_t rt_device_find(const char *);
int rt_kprintf(const char *, ...);
