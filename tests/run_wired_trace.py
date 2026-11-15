#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Check real status formatters through RT-Thread's 128-byte console limit."""
from pathlib import Path
import os
import subprocess
import tempfile

app = Path(__file__).resolve().parents[1]
driver = app.parents[2] / "packages/third-party/cherryusb-ipheth"


def function(path, signature):
    text = path.read_text(encoding="utf-8")
    start = text.index(signature)
    end = text.index("{", start) + 1
    depth = 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end] + "\n"


source = r'''
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ipheth_traffic.h"
#define rt_snprintf snprintf
static char output[16384];
static unsigned output_length, lines;
static int rt_kprintf(const char *format, ...) {
    char console[128];
    va_list args;
    va_start(args, format);
    int n = vsnprintf(console, sizeof(console), format, args);
    va_end(args);
    assert(n > 0 && n < (int)sizeof(console));
    assert(console[n-1] == '\n');
    assert(output_length + n < sizeof(output));
    memcpy(output + output_length, console, (size_t)n + 1);
    output_length += (unsigned)n;
    ++lines;
    return n;
}
static void clear_output(void) {
    output[0] = 0; output_length = 0; lines = 0;
}
'''
source += function(app / "app/carplay_usb_cmd.c", "static void iap2_usb_trace_bytes(")
source += function(driver / "port/rtthread/ipheth_rtthread.c", "static void neighbor_text(")
source += function(driver / "port/rtthread/ipheth_rtthread.c", "static void print_neighbors(")
source += r'''
static void check_bytes(size_t length) {
    static const char digits[] = "0123456789abcdef";
    unsigned char body[160];
    for (unsigned i = 0; i < sizeof(body); ++i) body[i] = (unsigned char)i;
    clear_output();
    iap2_usb_trace_bytes("wired_identifier_hex", body, length);
    size_t captured = length > 128 ? 128 : length;
    char expected[100];
    snprintf(expected, sizeof(expected),
        "bytes=%u captured=%u truncated=%u\n", (unsigned)length,
        (unsigned)captured, length > captured);
    assert(strstr(output, expected));
    assert(lines == 1 + (captured+15)/16);
    for (size_t offset = 0; offset < captured; offset += 16) {
        snprintf(expected, sizeof(expected), "offset=%u hex=", (unsigned)offset);
        const char *p = strstr(output, expected); assert(p);
        p += strlen(expected);
        size_t chunk = captured-offset;
        if (chunk > 16) chunk = 16;
        for (size_t i = 0; i < chunk; ++i) {
            assert(*p++ == digits[body[offset+i] >> 4]);
            assert(*p++ == digits[body[offset+i] & 15]);
        }
        assert(*p == '\n');
    }
}
int main(void) {
    const size_t lengths[] = {0, 1, 16, 17, 25, 64, 69, 128, 160};
    for (unsigned i = 0; i < sizeof(lengths)/sizeof(lengths[0]); ++i)
        check_bytes(lengths[i]);
    clear_output();
    iap2_usb_trace_bytes("raw", NULL, 0);
    assert(lines == 1);
    struct ipheth_traffic traffic = {0};
    clear_output(); print_neighbors(1, "RX", &traffic); assert(!lines);
    for (unsigned i = 0; i < 4; ++i) {
        struct ipheth_neighbor *r = &traffic.neighbors[i];
        memset(r, 0xff, sizeof(*r));
        r->ordinal = 4294967292U+i;
        r->type = 136;
    }
    clear_output(); print_neighbors(1, "RX", &traffic);
    assert(lines == 16);
    for (unsigned i = 0; i < 4; ++i) {
        const char *fields[] = {"src", "dst", "target"};
        for (unsigned f = 0; f < 3; ++f) {
            char expected[110];
            snprintf(expected, sizeof(expected),
                "[ipheth-nd] i1 RX #%u %s=ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff\n",
                traffic.neighbors[i].ordinal, fields[f]);
            assert(strstr(output, expected));
        }
    }
    puts("PASS: complete IPv6 targets and bounded iAP2 hex through 128-byte console");
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="carplay-wired-trace-") as folder:
    c = Path(folder) / "trace.c"
    exe = Path(folder) / "trace"
    c.write_text(source, encoding="utf-8")
    subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra",
                    "-Werror", "-g", "-fsanitize=address,undefined",
                    "-I" + str(driver / "port/rtthread"), str(c), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
