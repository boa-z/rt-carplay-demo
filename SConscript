# SPDX-License-Identifier: GPL-3.0-or-later
#
# Luban-Lite application integration for the bundled rt-carplay submodule.
# The default RTCARPLAY_DIR points at packages/third-party/rt-carplay; override
# it in menuconfig only when building against a separate checkout.
import os
from building import *

cwd = GetCurrentDir()

try:
    repo = GetConfigValue('RTCARPLAY_DIR')
except KeyError:
    repo = ''
repo = repo.strip('"')

if not repo:
    repo = os.path.join('..', '..', '..', 'packages', 'third-party', 'rt-carplay')
if not os.path.isabs(repo):
    repo = os.path.join(cwd, repo)
repo = os.path.normpath(repo)

src = Glob('main.c')
if GetDepend('RTCARPLAY_DEMO_LAUNCHER'):
    src += Glob('ui/*.c')
    src += Glob('app/*.c')
group = DefineGroup('Applications', src, depend=[''], CPPPATH=[cwd, cwd + '/app'],
                      CPPDEFINES=['MBEDTLS_CHACHA20_C', 'MBEDTLS_CHACHAPOLY_C',
                                  'MBEDTLS_HKDF_C'])

app_sconscript = os.path.join(repo, 'SConscript')
if not os.path.isfile(app_sconscript):
    raise RuntimeError(
        'rt-carplay source not found at ' + repo +
        '; run "git submodule update --init packages/third-party/rt-carplay"'
        ' or set CONFIG_RTCARPLAY_DIR')

import building
if not any(project.get('name') == 'rt-carplay' for project in building.Projects):
    group = group + SConscript(app_sconscript)

Return('group')
