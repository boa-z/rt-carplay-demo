/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdint.h>
/* Match the SDK framebuffer ABI, which uses type/number only. Keep the
 * guards: including sys/ioctl.h first must remain observable in tests. */
#ifndef _IOWR
#define _IOWR(x, y, z) (((x) << 8) | (y))
#endif
#ifndef _IOW
#define _IOW(x, y, z) (((x) << 8) | (y))
#endif
#ifndef _IOR
#define _IOR(x, y, z) (((x) << 8) | (y))
#endif
#ifndef _IO
#define _IO(x, y) (((x) << 8) | (y))
#endif
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;
