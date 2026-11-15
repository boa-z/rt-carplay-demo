/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "native_ncm.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t descriptor[] = {
    9,2, 0,0, 7,6,0,0x80,50,
    9,4, 2,1,2, 0xff,0xfd,1,0,
    7,5, 0x86,2,0,2,0, 7,5, 5,2,0,2,0,
    9,4, 3,0,1, 2,13,0,0,
    5,0x24,6,3,4,
    13,0x24,15,23,0,0,0,0,0x8e,0x3e,0,0,0,
    6,0x24,26,0,1,0x11,
    7,5,0x87,3,16,0,9,
    9,4,4,0,0,10,0,1,0,
    9,4,4,1,2,10,0,1,0,
    7,5,0x88,2,0,2,0, 7,5,6,2,0,2,0,
    9,4,5,0,0,2,13,0,0,
    5,0x24,6,5,6,
    13,0x24,15,21,0,0,0,0,0x8e,0x3e,0,0,0,
    6,0x24,26,0,1,0x11,
    9,4,6,0,0,10,0,1,0,
    9,4,6,1,2,10,0,1,0,
    7,5,0x89,2,0,2,0, 7,5,7,2,0,2,0
};
static uint8_t parameters[] = {28,0,1,0,0,0,1,0,4,0,0,0,4,0,0,0,
                               0,0x40,0,0,4,0,0,0,4,0,0,0};
static uint8_t frame[1514], block[8192];
static unsigned calls;
static int receive(void *arg, const uint8_t *p, size_t n)
{
    (void)arg;
    assert(n == sizeof(frame));
    assert(!memcmp(p, frame, n));
    ++calls; return 0;
}
static int bounded(void *arg, const uint8_t *p, size_t n)
{
    (void)arg;
    assert(p >= block + 12 && p + n <= block + sizeof(block));
    assert(n >= 14 && n <= 1514);
    return 0;
}
static void packet_headers(void)
{
    uint8_t p[128] = {0};
    struct carplay_ncm_packet info, same;
    p[0] = 0x33; p[6] = 2; p[12] = 0x86; p[13] = 0xdd;
    p[14] = 0x60; p[19] = 24; p[20] = 58; p[21] = 255;
    p[22] = 0xfe; p[23] = 0x80; p[37] = 1;
    p[38] = 0xff; p[39] = 2; p[53] = 2;
    p[54] = 135; p[62] = 0xfe; p[63] = 0x80; p[77] = 3;
    assert(carplay_ncm_packet(p, 78, &info) == 1);
    assert(info.neighbor && info.icmp_type == 135 && info.target[15] == 3);
    assert(info.source[15] == 1 && info.destination[15] == 2 && info.hop_limit == 255);
    assert(info.source_mac[0] == 2 && info.destination_mac[0] == 0x33);
    p[54] = 136;
    assert(carplay_ncm_packet(p, 78, &info) == 1 && info.icmp_type == 136);
    for (unsigned length = 0; length < 78; ++length)
        assert(carplay_ncm_packet(p, length, &info) < 0);
    p[20] = 6; p[19] = 28;
    memset(p + 54, 0, sizeof(p) - 54);
    p[54] = 0xc0; p[55] = 0x01; p[56] = 0x1b; p[57] = 0x58;
    p[66] = 0x50; p[67] = 2;
    assert(carplay_ncm_packet(p, 82, &info) == 1 && info.transport);
    assert(info.source_port == 49153 && info.destination_port == 7000 && info.tcp_flags == 2);
    memset(p + 74, 0xa5, 8);
    assert(carplay_ncm_packet(p, 82, &same) == 1 && !memcmp(&same, &info, sizeof(info)));
    p[66] = 0xf0;
    assert(carplay_ncm_packet(p, 82, &info) < 0);
    /* NS behind a hop-by-hop header. */
    memset(p + 54, 0, sizeof(p) - 54);
    p[20] = 0; p[19] = 32; p[54] = 58; p[62] = 135; p[85] = 7;
    assert(carplay_ncm_packet(p, 86, &info) == 1 && info.neighbor && info.target[15] == 7);
    p[55] = 0xff;
    assert(carplay_ncm_packet(p, 86, &info) < 0);
    /* A later fragment cannot be mistaken for a TCP header. */
    p[20] = 44; p[55] = 0; p[54] = 6; p[57] = 8;
    assert(carplay_ncm_packet(p, 86, &info) == 1 && info.fragmented && !info.transport);
    p[12] = 8; p[13] = 0;
    assert(carplay_ncm_packet(p, 14, &info) == 0 && !info.transport);
    puts("NCM headers: NS/NA targets, TCP ports, extensions, fragments, truncation and payload exclusion passed");
}
int main(void)
{
    packet_headers();
    descriptor[2] = sizeof(descriptor);
    descriptor[3] = sizeof(descriptor) >> 8;
    struct carplay_ncm_function f;
    assert(!carplay_ncm_find(descriptor, sizeof(descriptor), &f));
    assert(f.control == 3 && f.data == 4 && f.alternate == 1 && f.mac_string == 23);
    assert(f.in == 0x88 && f.out == 6 && f.notification == 0x87);
    assert(f.in_mps == 512 && f.out_mps == 512 && f.segment == 16014);
    /* The Apple 0x86/0x05 pair and second NCM function must not be selected. */
    for (size_t n = 0; n < sizeof(descriptor); ++n)
        assert(carplay_ncm_find(descriptor, n, &f) < 0);
    for (size_t at = 9; at < sizeof(descriptor);) {
        uint8_t n = descriptor[at]; descriptor[at] = 0;
        assert(carplay_ncm_find(descriptor, sizeof(descriptor), &f) < 0);
        descriptor[at] = n; at += n;
    }
    struct carplay_ncm_limits limits;
    assert(!carplay_ncm_parameters(parameters, sizeof(parameters), &limits));
    assert(limits.input == 16384 && limits.output == 8192);
    parameters[2] = 2;
    assert(carplay_ncm_parameters(parameters, sizeof(parameters), &limits) < 0);
    parameters[2] = 1; parameters[20] = 0;
    assert(carplay_ncm_parameters(parameters, sizeof(parameters), &limits) < 0);
    parameters[20] = 4; parameters[22] = 4;
    assert(carplay_ncm_parameters(parameters, sizeof(parameters), &limits) < 0);
    parameters[22] = 0;
    uint8_t mac[6], string[26] = {26,3};
    const char *text = "02abCDef1234";
    for (unsigned i = 0; i < 12; ++i) string[2 + 2*i] = text[i];
    assert(!carplay_ncm_mac(string, sizeof(string), mac) && mac[0] == 2 && mac[1] == 0xab);
    string[3] = 1;
    assert(carplay_ncm_mac(string, sizeof(string), mac) < 0);
    string[3] = 0; string[4] = '3';
    assert(carplay_ncm_mac(string, sizeof(string), mac) < 0);
    for (unsigned i = 0; i < sizeof(frame); ++i) frame[i] = i * 7;
    for (unsigned divisor = 1; divisor <= 32; divisor *= 2) {
        limits.divisor = divisor; limits.alignment = 4;
        for (unsigned rem = 0; rem < divisor; ++rem) {
            limits.remainder = rem;
            int n = carplay_ncm_encode(&limits, 0xffff, frame, sizeof(frame), block, sizeof(block));
            assert(n > 0);
            size_t table = block[10] | (unsigned)block[11] << 8;
            unsigned start = block[table + 8] | (unsigned)block[table + 9] << 8;
            assert((start + 14) % divisor == rem && !(table & 3));
            calls = 0;
            assert(carplay_ncm_decode(block, n, receive, NULL) == 1 && calls == 1);
            block[n] = 0;
            assert(carplay_ncm_decode(block, n + 1, receive, NULL) == 1);
            for (int short_size = 0; short_size < n; ++short_size)
                assert(carplay_ncm_decode(block, short_size, receive, NULL) < 0);
            /* Corrupt a later DPE: validate before delivering the first frame. */
            block[table + 12] = 1; calls = 0;
            assert(carplay_ncm_decode(block, n, receive, NULL) < 0 && !calls);
            block[table + 12] = 0;
            block[table + 6] = 1;
            assert(carplay_ncm_decode(block, n, receive, NULL) < 0);
            block[table + 6] = 0;
        }
    }
    limits.output = 64;
    assert(carplay_ncm_encode(&limits, 0, frame, sizeof(frame), block, sizeof(block)) < 0);
    limits.output = 8192;
    int n = carplay_ncm_encode(&limits, 0, frame, sizeof(frame), block, sizeof(block));
    for (int at = 0; at < n; ++at) {
        uint8_t keep = block[at];
        for (unsigned bit = 0; bit < 8; ++bit) {
            block[at] = keep ^ (1u << bit);
            carplay_ncm_decode(block, n, bounded, NULL);
        }
        block[at] = keep;
    }
    puts("native NCM: descriptor binding, negotiation, MAC, framing, corruption and bounds passed");
}
