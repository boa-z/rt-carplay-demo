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

src = Glob('main.c') + Glob('app/carplay_app.c')
if GetDepend('RTCARPLAY_DEMO_LAUNCHER'):
    src += Glob('ui/*.c') + Glob('app/connection_manager.c')
cpppath = [cwd, cwd + '/app', cwd + '/platform']
src += Glob('app/pairing_files.c') + Glob('platform/carplay_mfi_provider.c')
if GetDepend('RTCARPLAY_DEMO_LAUNCHER'):
    src += Glob('app/pairing_manager.c')
if GetDepend('RTCARPLAY_VIDEO'):
    src += Glob('platform/video_sink_lubanlite.c')
if GetDepend('RTCARPLAY_AUDIO'):
    src += Glob('platform/audio_sink_lubanlite.c')
    if GetDepend('RTCARPLAY_DEMO_LAUNCHER'):
        src += Glob('platform/audio_test_lubanlite.c')
if GetDepend('RTCARPLAY_MICROPHONE'):
    src += Glob('platform/audio_input_lubanlite.c')
if GetDepend('RTCARPLAY_TOUCH_PANEL'):
    src += Glob('platform/touch_panel_lubanlite.c')
if GetDepend('RTCARPLAY_IAP2'):
    src += Glob('app/iap2_link_lubanlite.c')
if GetDepend('RTCARPLAY_USB_HOST'):
    src += Glob('app/carplay_usb_cmd.c') + Glob('app/usb_pair_cache.c')

# Luban-Lite board seams.  The include directory is shared by the Wi-Fi,
# Bluetooth and MFi sources (rtcp_lubanlite.h declares all three interfaces).
if (GetDepend('LPKG_RT_CARPLAY_LUBANLITE_WIFI') or
        GetDepend('LPKG_RT_CARPLAY_LUBANLITE_BT') or
        GetDepend('LPKG_RT_CARPLAY_LUBANLITE_MFI') or
        GetDepend('LPKG_RT_CARPLAY_LUBANLITE_USB')):
    cpppath.append(cwd + '/platform')

# Luban-Lite Wi-Fi seam (Realtek RTL8733BS SoftAP + vendor IE).
if GetDepend('LPKG_RT_CARPLAY_LUBANLITE_WIFI'):
    src += Glob('platform/rtcp_wifi_lubanlite.c')

    # The Realtek API headers live outside the SDK's global include path;
    # the driver SConscript adds them only to its own group.
    aic_root = os.environ.get('AIC_ROOT', '')
    if aic_root:
        realtek = os.path.join(aic_root, 'bsp', 'peripheral', 'wireless',
                               'realtek')
        for sub in ('api/wifi', 'api', 'include', 'os/os_dep/include',
                    'platform/include', 'platform/rtwlan_bsp'):
            cpppath.append(os.path.join(realtek, sub))

# CE TRNG, used for AirPlay pairing and for the wired lockdown pair record.
if (GetDepend('LPKG_RT_CARPLAY_LUBANLITE_WIFI') or
        GetDepend('LPKG_RT_CARPLAY_LUBANLITE_USB')):
    src += Glob('platform/rtcp_random_lubanlite.c')

# Luban-Lite Bluetooth seam: Zephyr Classic BR/EDR + RFCOMM for iAP2.
if GetDepend('LPKG_RT_CARPLAY_LUBANLITE_BT'):
    src += Glob('platform/rtcp_bt_lubanlite.c')

    # The zephyr-bluetooth package ships no settings core; this file backs
    # CONFIG_BT_SETTINGS with an RT-Thread DFS file so BR/EDR link keys (and
    # therefore the phone's bond) survive a reboot.
    if GetDepend('LPKG_BT_SETTINGS'):
        src += Glob('platform/rtcp_bt_settings.c')

    aic_root = os.environ.get('AIC_ROOT', '')
    if aic_root:
        zephyr = os.path.join(aic_root, 'packages', 'third-party',
                              'zephyr-bluetooth')
        cpppath.append(os.path.join(zephyr, 'include'))
        cpppath.append(os.path.join(zephyr, 'porting'))

# Luban-Lite MFi seam: 2.0C authentication coprocessor on the I2C bus.
if GetDepend('LPKG_RT_CARPLAY_LUBANLITE_MFI'):
    src += Glob('platform/rtcp_mfi_lubanlite.c')

if GetDepend('RTCARPLAY_MICROPHONE'):
    src += Glob('platform/rtcp_audio_capture_lubanlite.c')
    cpppath.append(cwd + '/platform')

# Luban-Lite wired USB host seam: CherryUSB host stack, Apple USBMUX class
# driver and the configuration-selection hook.  The CherryUSB headers are
# only added to this group (the stack builds its own copy with its own paths).
if GetDepend('LPKG_RT_CARPLAY_LUBANLITE_USB'):
    src += Glob('platform/rtcp_usb_lubanlite.c')

    # NCM netdev helpers; the CherryUSB NCM class needs lwIP.
    if GetDepend('RT_USING_LWIP'):
        src += Glob('platform/rtcp_net_lubanlite.c')

    aic_root = os.environ.get('AIC_ROOT', '')
    if aic_root:
        cherryusb = os.path.join(aic_root, 'packages', 'third-party',
                                 'cherryusb')
        cpppath.append(os.path.join(cherryusb, 'common'))
        cpppath.append(os.path.join(cherryusb, 'core'))

# These sources previously inherited the SDK group's force-included logger.
# Keep that provider local to the host group after moving the implementations.
ccflags = ''
if GetDepend('LPKG_RT_CARPLAY_LOG'):
    ccflags = '-include ' + os.path.join(
        repo, 'port', 'rtthread', 'include', 'rtcp_log_rtthread.h')

group = DefineGroup('Applications', src, depend=[''], CPPPATH=cpppath,
                    LOCAL_CCFLAGS=ccflags,
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
