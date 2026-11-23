/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdint.h>
#define rt_snprintf snprintf
typedef struct test_mutex *rt_mutex_t;
typedef struct test_thread *rt_thread_t;
typedef unsigned long rt_base_t;
#define RT_EOK             0
#define RT_WAITING_NO      0
#define RT_WAITING_FOREVER (-1)
#define RT_IPC_FLAG_FIFO   0
rt_mutex_t rt_mutex_create(const char *, int);
int rt_mutex_take(rt_mutex_t, int);
int rt_mutex_release(rt_mutex_t);
int rt_mutex_delete(rt_mutex_t);
int rt_kprintf(const char *, ...);
uint32_t rt_tick_get_millisecond(void);
rt_base_t rt_hw_interrupt_disable(void);
void rt_hw_interrupt_enable(rt_base_t);
void rt_thread_mdelay(int);
rt_thread_t rt_thread_create(const char *, void (*)(void *), void *, unsigned, unsigned, unsigned);
int rt_thread_startup(rt_thread_t);
int rt_thread_delete(rt_thread_t);
rt_thread_t rt_thread_self(void);
#define MSH_CMD_EXPORT_ALIAS(fn, name, description)
