#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=$(CDPATH= cd -- "$here/../../../../.." && pwd)
lwip="$root/kernel/rt-thread/components/net/lwip/lwip-2.1.2"
build="$1"
mkdir -p "$build"
cc -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter -g \
 -fsanitize=address,undefined -fno-pie -no-pie -ffunction-sections -fdata-sections \
 -I"$here" -I"$lwip/src/include" \
 -DLWIP_API_MSG_SOURCE='"'"$lwip/src/api/api_msg.c"'"' \
 "$here/burst.c" "$lwip/src/api/netbuf.c" "$lwip/src/core/init.c" \
 "$lwip/src/core/mem.c" "$lwip/src/core/memp.c" "$lwip/src/core/pbuf.c" \
 -Wl,--gc-sections -o "$build/test_udp_burst"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$build/test_udp_burst"

python3 "$here/check_config.py"

cc -std=c11 -Wall -Wextra -Werror -Wno-unused-function -g \
 -fsanitize=address,undefined -fno-pie -no-pie -ffunction-sections -fdata-sections \
 -I"$here/rtthread_net" -I"$here" -I"$lwip/src/include" \
 -I"$root/packages/third-party/cherryusb-ipheth/include" -DLWIP_NETIF_API=1 \
 -DIPHETH_RTTHREAD_SOURCE='"'"$root/packages/third-party/cherryusb-ipheth/port/rtthread/ipheth_rtthread.c"'"' \
 "$here/link.c" -Wl,--gc-sections -o "$build/test_ipheth_link"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$build/test_ipheth_link"
