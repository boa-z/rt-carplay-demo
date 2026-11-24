#pragma once
#include <stdint.h>
#include <stdarg.h>
struct rt_device
{
    void *user_data;
};
typedef struct rt_device *rt_device_t;
typedef void *rt_thread_t;
struct rt_semaphore
{
    int count;
};
#define RT_EOK             0
#define RT_ENOMEM          12
#define RT_ERROR           1
#define RT_EBUSY           16
#define RT_EINVAL          22
#define RT_IPC_FLAG_FIFO   0
#define RT_WAITING_FOREVER (-1)
rt_device_t rt_device_find(const char *);
int rt_sem_init(struct rt_semaphore *, const char *, int, int);
int rt_sem_release(struct rt_semaphore *);
int rt_sem_take(struct rt_semaphore *, int);
int rt_sem_detach(struct rt_semaphore *);
int rt_kprintf(const char *, ...);
rt_thread_t rt_thread_create(const char *, void (*)(void *), void *, unsigned, unsigned, unsigned);
int rt_thread_startup(rt_thread_t);
int rt_thread_delete(rt_thread_t);
void rt_thread_mdelay(int);
#define INIT_APP_EXPORT(fn)                   static void *init_ref __attribute__((unused)) = (void *)&fn
#define MSH_CMD_EXPORT(fn, desc)              static void *shell_ref __attribute__((unused)) = (void *)&fn
#define MSH_CMD_EXPORT_ALIAS(fn, alias, desc) MSH_CMD_EXPORT(fn, desc)

typedef long rt_base_t;
rt_base_t rt_hw_interrupt_disable(void);
void rt_hw_interrupt_enable(rt_base_t);
int rt_tick_from_millisecond(int);
