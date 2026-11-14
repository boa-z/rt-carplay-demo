#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise actual iAP2 provider functions and retained callback strings."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
rtcp = Path(os.environ.get("RTCP_ROOT", root.parents[2] / "packages/third-party/rt-carplay"))


def function(path, signature):
    text = (root / path).read_text(encoding="utf-8")
    start = text.index(signature)
    end = text.index("{", start) + 1
    depth = 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end]


source = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "carplay_app.h"
#include "iap2_session.h"
#include "rtcp/rtcp.h"
#define rt_snprintf snprintf
#define rt_kprintf(...) ((void)0)
typedef struct {
    char ip_address[16];
    carplay_app_iap2_info_t endpoint;
} iap2_link_t;
typedef struct {
    bool airplay_unavailable_logged;
    char ip_address[64];
    carplay_app_wired_info_t endpoint;
} iap2_usb_link_t;
static carplay_app_iap2_info_t wireless;
static carplay_app_wired_info_t wired;
static bool available = true;
int carplay_app_iap2_info(carplay_app_iap2_info_t *out) {
    memset(out, 0, sizeof(*out));
    if (!available) return RTCP_ESTATE;
    *out = wireless; return RTCP_OK;
}
int carplay_app_wired_info(carplay_app_wired_info_t *out) {
    memset(out, 0, sizeof(*out));
    if (!available) return RTCP_ESTATE;
    *out = wired; return RTCP_OK;
}
'''
source += function("app/iap2_link_lubanlite.c", "static bool link_get_wifi(")
source += function("app/iap2_link_lubanlite.c", "static bool link_get_airplay(")
source += function("app/carplay_usb_cmd.c", "static bool iap2_usb_get_airplay(")
source += r'''
int main(void) {
    strcpy(wireless.ssid, "ownership-fixture");
    strcpy(wireless.passphrase, "public-test-fixture");
    strcpy(wireless.device_id, "02:00:00:00:00:01");
    strcpy(wireless.public_key_hex, "public-test-key");
    wireless.ipv4[0] = 192; wireless.ipv4[1] = 168; wireless.ipv4[2] = 1; wireless.ipv4[3] = 1;
    wireless.airplay_port = 7000; wireless.mdns_ready = true;
    wireless.source_version = "test";
    strcpy(wired.device_id, wireless.device_id);
    strcpy(wired.public_key_hex, wireless.public_key_hex);
    strcpy(wired.ipv6, "fe80::1234"); wired.airplay_port = 7000;
    wired.source_version = "test";
    iap2_link_t link = {0};
    iap2_usb_link_t usb = {0};
    iap2_session_wifi_t wifi;
    iap2_session_airplay_t airplay, usb_airplay;
    assert(link_get_wifi(&wifi, &link));
    assert(wifi.ssid == link.endpoint.ssid && wifi.passphrase == link.endpoint.passphrase);
    assert(link_get_airplay(&airplay, &link));
    assert(airplay.device_identifier == link.endpoint.device_id);
    assert(airplay.public_key == link.endpoint.public_key_hex);
    assert(airplay.ip_address == link.ip_address);
    assert(iap2_usb_get_airplay(&usb_airplay, &usb));
    assert(usb_airplay.device_identifier == usb.endpoint.device_id);
    assert(usb_airplay.public_key == usb.endpoint.public_key_hex);
    assert(usb_airplay.ip_address == usb.ip_address);
    /* Retained pointers must survive provider return and source retirement. */
    memset(&wireless, 0, sizeof(wireless)); memset(&wired, 0, sizeof(wired));
    assert(!strcmp(wifi.ssid, "ownership-fixture"));
    assert(!strcmp(airplay.device_identifier, "02:00:00:00:00:01"));
    assert(!strcmp(usb_airplay.public_key, "public-test-key"));
    assert(!strcmp(usb_airplay.ip_address, "fe80::1234"));
    available = false;
    iap2_session_wifi_t ignored_wifi;
    iap2_session_airplay_t ignored_airplay;
    assert(!link_get_wifi(&ignored_wifi, &link));
    assert(!link_get_airplay(&ignored_airplay, &link));
    assert(!iap2_usb_get_airplay(&ignored_airplay, &usb));
    assert(!strcmp(wifi.ssid, "ownership-fixture"));
    assert(!strcmp(airplay.device_identifier, "02:00:00:00:00:01"));
    assert(!strcmp(usb_airplay.public_key, "public-test-key"));
    puts("Actual iAP2 providers: link-owned strings survive callback return, source retirement and failed refresh");
}
'''
with tempfile.TemporaryDirectory(prefix="iap2-endpoint-ownership-") as directory:
    path = Path(directory)
    (path / "test.c").write_text(source, encoding="utf-8")
    subprocess.run([
        os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
        "-fsanitize=address,undefined", "-fno-pie", "-no-pie",
        "-I" + str(root / "app"), "-I" + str(rtcp / "include"),
        "-I" + str(rtcp / "core/iap2/include"),
        str(path / "test.c"), "-o", str(path / "test"),
    ], check=True)
    subprocess.run([str(path / "test")], check=True)
