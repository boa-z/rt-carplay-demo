# SPDX-License-Identifier: GPL-3.0-or-later
# Host regressions for the actual Luban-Lite board composition and backends.
.DEFAULT_GOAL := check
CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -MMD -MP
SDK_ROOT ?= $(abspath ../../..)
RTCP_ROOT ?= $(SDK_ROOT)/packages/third-party/rt-carplay
CHERRYUSB_ROOT ?= $(SDK_ROOT)/packages/third-party/cherryusb
BUILD ?= build
RTCP_BUILD := $(RTCP_ROOT)/build
LIB := $(RTCP_BUILD)/librtcp_core.a
PORT_LINUX_LIB := $(RTCP_BUILD)/librtcp_port_linux.a
MBEDTLS_LIB := $(RTCP_BUILD)/mbedtls/libmbedcrypto.a
AUDIO_LIBS := $(RTCP_BUILD)/audio-deps/librtcp_faad.a $(RTCP_BUILD)/audio-deps/librtcp_opus.a $(RTCP_BUILD)/audio-deps/librtcp_speexdsp.a
LDLIBS := $(AUDIO_LIBS) -lm
CPPFLAGS += $(addprefix -I$(RTCP_ROOT)/,include core core/mfi core/iap2/include core/airplay/include core/crypto/include core/media/include core/input/include core/discovery/include core/usb/include core/audio/include port/rtthread/include third_party/ed25519 port/linux/include third_party/mbedtls/include config) -Iapp -Iplatform
CPPFLAGS += -DRTCP_AUDIO_CODECS -DMBEDTLS_CONFIG_FILE='"mbedtls_rtcp_host.h"'
BOARD_BINS := $(addprefix $(BUILD)/,test_audio_diagnostics test_audio_input test_audio_sink test_pairing_files test_rtthread_app)
export RTCP_ROOT
$(BUILD):
	@mkdir -p $@
.PHONY: sdk-libraries check
sdk-libraries:
	$(MAKE) -C $(RTCP_ROOT) $(patsubst $(RTCP_ROOT)/%,%,$(LIB) $(PORT_LINUX_LIB) $(MBEDTLS_LIB) $(AUDIO_LIBS))
$(BOARD_BINS): | sdk-libraries $(BUILD)
$(LIB) $(PORT_LINUX_LIB) $(MBEDTLS_LIB) $(AUDIO_LIBS): sdk-libraries
check: $(BOARD_BINS)
	@set -e; for t in $(BOARD_BINS); do ./$$t; done
	$(MAKE) check-lubanlite-video check-lubanlite-touch check-lubanlite-demo check-lubanlite-bonds check-lubanlite-usb check-lubanlite-handoff check-lubanlite-reconnect check-lubanlite-capabilities check-iap2-endpoints check-usb-iap2-worker check-wireless-iap2-worker
$(BUILD)/test_pairing_files: tests/unit/test_pairing_files.c app/pairing_files.c app/pairing_manager.c app/usb_pair_cache.c $(LIB) $(MBEDTLS_LIB)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) $< $(LIB) $(MBEDTLS_LIB) $(LDLIBS) -o $@

CPPFLAGS += -DRTCP_AUDIO_FIXTURE='"$(RTCP_ROOT)/tests/fixtures/audio/sine-44k.aac"'

# Optional SDK-port regression using official CherryUSB headers (no SDK build).
# make check-lubanlite-usb CHERRYUSB_ROOT=/path/to/CherryUSB
.PHONY: check-lubanlite-usb
check-lubanlite-usb:
	@test -f "$(CHERRYUSB_ROOT)/core/usbh_core.h" || (echo "Set CHERRYUSB_ROOT to official CherryUSB"; exit 1)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -DLPKG_RT_CARPLAY_LUBANLITE_USB \
	  -Itests/support/lubanlite_usb -Iplatform \
	  -I$(CHERRYUSB_ROOT)/core -I$(CHERRYUSB_ROOT)/common \
	  -I$(CHERRYUSB_ROOT)/class/hub -ffunction-sections -fdata-sections \
	  tests/unit/test_lubanlite_usb_dma.c $(RTCP_ROOT)/core/usb/usb_device.c -Wl,--gc-sections -o $(BUILD)/test_lubanlite_usb_dma
	$(BUILD)/test_lubanlite_usb_dma

# Actual RT-Thread composition with Linux protocol ports and modeled scheduling.
.PHONY: check-rtthread-app
check-rtthread-app: $(BUILD)/test_rtthread_app
	$(BUILD)/test_rtthread_app

$(BUILD)/test_rtthread_app: tests/unit/test_rtthread_app.c app/carplay_app.c \
    app/carplay_app.h app/pairing_files.c \
    tests/support/rtthread_app/rtthread.h $(RTCP_ROOT)/port/linux/rtcp_os_linux.c \
    $(RTCP_ROOT)/port/linux/rtcp_net_linux.c $(LIB) $(MBEDTLS_LIB) $(AUDIO_LIBS)
	@mkdir -p $(BUILD)/rtthread-app
	$(CC) $(CFLAGS) $(CPPFLAGS) -Drtcp_os_linux=rtcp_os_rtthread -c $(RTCP_ROOT)/port/linux/rtcp_os_linux.c -o $(BUILD)/rtthread-app/os.o
	$(CC) $(CFLAGS) $(CPPFLAGS) -Drtcp_net_linux=rtcp_net_rtthread -c $(RTCP_ROOT)/port/linux/rtcp_net_linux.c -o $(BUILD)/rtthread-app/net.o
	$(CC) $(CFLAGS) $(CPPFLAGS) -Itests/support/rtthread_app -Iplatform \
	  -Wno-unused-function -ffunction-sections -fdata-sections tests/unit/test_rtthread_app.c \
	  $(BUILD)/rtthread-app/os.o $(BUILD)/rtthread-app/net.o $(LIB) $(MBEDTLS_LIB) $(LDLIBS) \
	  -pthread -Wl,--gc-sections -o $(BUILD)/test_rtthread_app

# Actual video sink with a bounded MPP model and the SDK's public headers.
.PHONY: check-lubanlite-video
check-lubanlite-video:
	@test -f "$(SDK_ROOT)/packages/artinchip/mpp/include/mpp_decoder.h" || (echo "Set SDK_ROOT to Luban-Lite SDK"; exit 1)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -Itests/support/lubanlite_video \
	  -I$(SDK_ROOT)/packages/artinchip/mpp/include \
	  -I$(SDK_ROOT)/bsp/artinchip/include/uapi \
	  -ffunction-sections -fdata-sections tests/unit/test_lubanlite_video.c \
	  -Wl,--gc-sections -o $(BUILD)/test_lubanlite_video
	$(BUILD)/test_lubanlite_video
	$(BUILD)/test_lubanlite_video queue
	$(CC) $(CFLAGS) $(CPPFLAGS) -Itests/support/lubanlite_video \
	  -I$(SDK_ROOT)/bsp/artinchip/include/uapi \
	  $(SDK_ROOT)/application/rt-thread/rtcarplay/tests/test_video_layer_policy.c \
	  -o $(BUILD)/test_host_video_policy
	$(BUILD)/test_host_video_policy

.PHONY: check-lubanlite-touch
check-lubanlite-touch:
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -Itests/support/lubanlite_touch \
	  tests/unit/test_lubanlite_touch.c -o $(BUILD)/test_lubanlite_touch
	$(BUILD)/test_lubanlite_touch

# Dual-mode demo lifecycle with actual SDK LVGL public headers.
.PHONY: check-lubanlite-demo
check-lubanlite-demo:
	@test -n "$(SDK_ROOT)" || (echo 'set SDK_ROOT to Luban-Lite checkout'; exit 1)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -Itests/support/lubanlite_demo -Iplatform \
	  -MF $(BUILD)/connection-manager-minimal.d -fsyntax-only $(SDK_ROOT)/application/rt-thread/rtcarplay/app/connection_manager.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -DRTCARPLAY_AUDIO -DRTCARPLAY_IAP2 -DLPKG_RT_CARPLAY_LUBANLITE_WIFI \
	  -Itests/support/lubanlite_demo -Iplatform \
	  -MF $(BUILD)/connection-manager-full.d -fsyntax-only $(SDK_ROOT)/application/rt-thread/rtcarplay/app/connection_manager.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -DLV_CONF_SKIP -DRTCARPLAY_AUDIO -DRTCARPLAY_IAP2 -DLPKG_RT_CARPLAY_LUBANLITE_WIFI \
	  -Itests/support/lubanlite_demo -Iplatform \
	  -I$(SDK_ROOT)/application/rt-thread/rtcarplay/app \
	  -I$(SDK_ROOT)/packages/artinchip/lvgl-ui/lvgl_v9/lvgl \
	  -DRTCP_CONNECTION_SOURCE='"$(abspath $(SDK_ROOT))/application/rt-thread/rtcarplay/app/connection_manager.c"' \
	  -DRTCP_LAUNCHER_SOURCE='"$(abspath $(SDK_ROOT))/application/rt-thread/rtcarplay/ui/lvgl_launcher.c"' \
	  -ffunction-sections -fdata-sections tests/unit/test_lubanlite_demo.c app/pairing_manager.c \
	  -Wl,--gc-sections -o $(BUILD)/test_lubanlite_demo
	$(BUILD)/test_lubanlite_demo

.PHONY: check-lubanlite-bonds
check-lubanlite-bonds:
	@test -n "$(SDK_ROOT)" || (echo 'set SDK_ROOT to Luban-Lite checkout'; exit 1)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -DCONFIG_X86 -DCONFIG_64BIT -DRT_IPC_FLAG_FIFO=0 \
	  -Itests/support/lubanlite_demo \
	  -I$(SDK_ROOT)/packages/third-party/zephyr-bluetooth/include \
	  -I$(SDK_ROOT)/packages/third-party/zephyr-bluetooth/porting \
	  tests/unit/test_lubanlite_bt_settings.c \
	  -Wl,-T,tests/support/lubanlite_demo/settings_sections.ld -o $(BUILD)/test_lubanlite_bt_settings
	$(BUILD)/test_lubanlite_bt_settings
	$(BUILD)/test_lubanlite_bt_settings negative-errno
	SDK_ROOT=$(SDK_ROOT) python3 tests/run_lubanlite_br_restore.py

.PHONY: check-lubanlite-handoff
check-lubanlite-handoff:
	python3 tests/run_lubanlite_handoff.py

.PHONY: check-lubanlite-reconnect
check-lubanlite-reconnect:
	python3 tests/run_lubanlite_reconnect.py

.PHONY: check-lubanlite-capabilities
check-lubanlite-capabilities:
	python3 tests/run_lubanlite_capabilities.py $(SDK_ROOT)

.PHONY: check-iap2-endpoints
check-iap2-endpoints:
	python3 tests/run_iap2_endpoint_ownership.py

# Offline speaker/microphone test worker against virtual devices.
$(BUILD)/test_audio_diagnostics: tests/unit/test_audio_diagnostics.c platform/audio_test_lubanlite.c platform/audio_test_fixture.h $(LIB) $(MBEDTLS_LIB)
	$(CC) $(CFLAGS) $(CPPFLAGS) -DRTCARPLAY_MICROPHONE -Itests/support/lubanlite_audio -Iplatform -o $@ $< $(LIB) $(MBEDTLS_LIB) $(LDLIBS) -lpthread

# Actual board sink with a pthread-backed RT-Thread/device seam.
$(BUILD)/test_audio_sink: tests/unit/test_audio_sink.c platform/audio_sink_lubanlite.c tests/support/lubanlite_audio/rtthread_host.c $(LIB) $(MBEDTLS_LIB)
	$(CC) $(CFLAGS) $(CPPFLAGS) -Itests/support/lubanlite_audio -o $@ $< tests/support/lubanlite_audio/rtthread_host.c $(LIB) $(MBEDTLS_LIB) $(LDLIBS) -lpthread

$(BUILD)/test_audio_input: tests/unit/test_audio_input.c platform/audio_input_lubanlite.c tests/support/lubanlite_audio/rtthread_host.c $(LIB) $(PORT_LINUX_LIB) $(MBEDTLS_LIB)
	$(CC) $(CFLAGS) $(CPPFLAGS) -Itests/support/lubanlite_audio -Iplatform -o $@ $< tests/support/lubanlite_audio/rtthread_host.c $(LIB) $(PORT_LINUX_LIB) $(MBEDTLS_LIB) $(LDLIBS) -lpthread


-include $(wildcard $(BUILD)/*.d)

.PHONY: check-usb-iap2-worker
check-usb-iap2-worker: sdk-libraries
	python3 tests/run_usb_iap2_worker.py

.PHONY: check-wireless-iap2-worker
check-wireless-iap2-worker: sdk-libraries
	python3 tests/run_wireless_iap2_worker.py
