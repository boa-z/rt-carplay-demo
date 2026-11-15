/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../lwipopts.h"
#define LWIP_TIMERS 0
#define LWIP_TCPIP_CORE_LOCKING 1
#ifdef TEST_DIRECT_INPUT
#define LWIP_TCPIP_CORE_LOCKING_INPUT 1
#else
#define LWIP_TCPIP_CORE_LOCKING_INPUT 0
#endif
