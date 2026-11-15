/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CARPLAY_TEST_SYS_ARCH_H
#define CARPLAY_TEST_SYS_ARCH_H
typedef unsigned sys_sem_t, sys_mutex_t, sys_thread_t;
typedef struct { void *items[64]; unsigned count, capacity; } sys_mbox_t;
#define sys_mbox_valid(m) ((m) && (m)->capacity)
#define sys_mbox_valid_val(m) ((m).capacity)
#define sys_mbox_set_invalid(m) ((m)->capacity = 0)
#define sys_sem_valid(m) ((m) && *(m))
#define sys_sem_set_invalid(m) (*(m) = 0)
#endif
