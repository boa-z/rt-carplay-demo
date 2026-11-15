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

for input_variant in queued direct; do
input_flags=''
if [ "$input_variant" = direct ]; then input_flags='-DTEST_DIRECT_INPUT'; fi
cc -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-unused-function -g \
 -fsanitize=address,undefined -fno-pie -no-pie -ffunction-sections -fdata-sections \
 -I"$here/queued" -I"$here" -I"$lwip/src/include" \
 -DLWIP_TCPIP_SOURCE='"'"$lwip/src/api/tcpip.c"'"' \
 $input_flags \
 "$here/queued_input.c" "$lwip/src/core/mem.c" "$lwip/src/core/memp.c" \
 "$lwip/src/core/pbuf.c" -Wl,--gc-sections -o "$build/test_${input_variant}_input"
done
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$build/test_queued_input"
python3 - "$build/test_direct_input" <<'PY'
import resource
import subprocess
import sys
resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
result = subprocess.run([sys.argv[1]], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
assert result.returncode != 0 and b'!in_rx' in result.stderr, result.stderr
print('actual lwIP direct input: expected RX-context assertion reproduced')
PY

python3 "$here/check_config.py"

cc -std=c11 -Wall -Wextra -Werror -Wno-unused-function -g \
 -fsanitize=address,undefined -fno-pie -no-pie -ffunction-sections -fdata-sections \
 -I"$here/rtthread_net" -I"$here" -I"$lwip/src/include" \
 -I"$root/packages/third-party/cherryusb-ipheth/include" -DLWIP_NETIF_API=1 \
 -DIPHETH_RTTHREAD_SOURCE='"'"$root/packages/third-party/cherryusb-ipheth/port/rtthread/ipheth_rtthread.c"'"' \
 "$here/link.c" -Wl,--gc-sections -o "$build/test_ipheth_link"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$build/test_ipheth_link"
