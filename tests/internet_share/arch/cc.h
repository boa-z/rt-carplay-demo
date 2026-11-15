#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#ifndef BYTE_ORDER
#define BYTE_ORDER LITTLE_ENDIAN
#endif
#define LWIP_PLATFORM_DIAG(x) do { printf x; } while (0)
#define LWIP_PLATFORM_ASSERT(x) do { fprintf(stderr, "%s\n", x); abort(); } while (0)
