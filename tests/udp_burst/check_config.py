# SPDX-License-Identifier: GPL-3.0-or-later
"""Compile actual port options with the GUI defconfig and legacy defaults."""
from pathlib import Path
import os
import re
import subprocess
import tempfile
root = Path(os.environ.get('SDK_ROOT', Path(__file__).resolve().parents[5])).resolve()
port = root / 'kernel/rt-thread/components/net/lwip/port'
for variant in ('rtcarplay', 'legacy', 'aic8800'):
    with tempfile.TemporaryDirectory() as directory:
        work = Path(directory)
        (work/'sys').mkdir()
        (work/'sys/errno.h').write_text('#include <errno.h>\n')
        config = ''
        if variant.startswith('rtcarplay'):
            text = (root/('target/configs/d21x_demo128-nand_rt-thread_' + variant + '_defconfig')).read_text()
            for name in ('RT_LWIP_UDP_RECVMBOX_SIZE', 'RT_LWIP_RECV_BUFSIZE'):
                value = re.search(r'^CONFIG_' + name + r'=(\d+)$', text, re.M)
                assert value, name
                config += '#define {} {}\n'.format(name, value[1])
            expected = (64,65536)
            assert re.search(r'^CONFIG_RT_LWIP_QUEUED_INPUT=y$', text, re.M)
            config += '#define RT_LWIP_QUEUED_INPUT 1\n'
            expected_input = 0
        else:
            expected = (32 if variant == 'aic8800' else 1,8192)
            expected_input = 1
            if variant == 'aic8800': config = '#define AIC_WLAN_AIC8800D40L 1\n'
        (work/'rtconfig.h').write_text(config)
        (work/'verify.c').write_text('#include "lwipopts.h"\n'
          '_Static_assert(DEFAULT_UDP_RECVMBOX_SIZE == %u, "UDP slots");\n'
          '_Static_assert(RECV_BUFSIZE_DEFAULT == %u, "UDP bytes");\n' % expected +
          '_Static_assert(LWIP_TCPIP_CORE_LOCKING_INPUT == %u, "input context");\n' % expected_input)
        subprocess.run(['cc','-std=c11','-fsyntax-only','-I'+str(work),'-I'+str(port),str(work/'verify.c')],check=True)
        print('actual lwipopts {}: slots={} bytes={}'.format(variant,*expected))
