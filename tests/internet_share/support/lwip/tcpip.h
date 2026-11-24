#include "lwip/err.h"
#define LOCK_TCPIP_CORE()   ((void)0)
#define UNLOCK_TCPIP_CORE() ((void)0)
err_t tcpip_callback(void (*fn)(void *), void *ctx);
