/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef TEST_USBD_CORE_H
#define TEST_USBD_CORE_H
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define USB_MEM_ALIGNX
#define USB_LOG_INFO(...)        \
    do                           \
    {                            \
        if (0)                   \
            printf(__VA_ARGS__); \
    } while (0)
#define USB_LOG_ERR(...)         \
    do                           \
    {                            \
        if (0)                   \
            printf(__VA_ARGS__); \
    } while (0)
struct usbd_interface;
#endif
