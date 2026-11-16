/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "native_ncm.h"
#include <string.h>

static uint16_t get16(const uint8_t *p)
{
    return p[0] | (uint16_t)p[1] << 8;
}
static uint32_t get32(const uint8_t *p)
{
    return get16(p) | (uint32_t)get16(p + 2) << 16;
}
static void put16(uint8_t *p, unsigned n)
{
    p[0] = n;
    p[1] = n >> 8;
}
static void put32(uint8_t *p, uint32_t n)
{
    put16(p, n);
    put16(p + 2, n >> 16);
}

static uint16_t network16(const uint8_t *p)
{
    return (uint16_t)p[0] << 8 | p[1];
}
int carplay_ncm_packet(const uint8_t *p, size_t size, struct carplay_ncm_packet *out)
{
    if (!p || !out)
        return -1;
    memset(out, 0, sizeof(*out));
    if (size < 14)
        return -1;
    memcpy(out->destination_mac, p, 6);
    memcpy(out->source_mac, p + 6, 6);
    out->ether_type = network16(p + 12);
    if (out->ether_type != 0x86dd)
        return 0;
    if (size < 54 || p[14] >> 4 != 6)
        return -1;
    size_t end = 54 + network16(p + 18), at = 54;
    if (end > size)
        return -1;
    out->protocol = p[20];
    out->hop_limit = p[21];
    memcpy(out->source, p + 22, 16);
    memcpy(out->destination, p + 38, 16);
    /* Bounded extension walk; non-initial fragments have no transport header. */
    for (unsigned count = 0; count < 8; ++count)
    {
        uint8_t next = out->protocol;
        if (next != 0 && next != 43 && next != 60 && next != 44 && next != 51)
            break;
        if (end - at < 8)
            return -1;
        size_t length = next == 44 ? 8 : next == 51 ? (p[at + 1] + 2u) * 4u
                                                    : (p[at + 1] + 1u) * 8u;
        if (length > end - at)
            return -1;
        out->protocol = p[at];
        if (next == 44)
        {
            out->fragmented = true;
            if (network16(p + at + 2) & 0xfff8)
                return 1;
        }
        at += length;
        if (count == 7)
            return -1;
    }
    if (out->protocol == 58)
    {
        if (end - at < 4)
            return -1;
        out->icmp_type = p[at];
        if (p[at] == 135 || p[at] == 136)
        {
            if (end - at < 24)
                return -1;
            memcpy(out->target, p + at + 8, 16);
            out->neighbor = true;
        }
    }
    else if (out->protocol == 6 || out->protocol == 17)
    {
        size_t minimum = out->protocol == 6 ? 20 : 8;
        if (end - at < minimum)
            return -1;
        if (out->protocol == 6)
        {
            size_t header = (p[at + 12] >> 4) * 4u;
            if (header < 20 || header > end - at)
                return -1;
            out->tcp_flags = p[at + 13];
        }
        out->source_port = network16(p + at);
        out->destination_port = network16(p + at + 2);
        out->transport = true;
    }
    return 1;
}

int carplay_ncm_find(const uint8_t *p, size_t size, struct carplay_ncm_function *out)
{
    if (!p || !out || size < 9 || p[0] != 9 || p[1] != 2 || get16(p + 2) != size)
        return -1;
    struct carplay_ncm_function f = { 0 };
    unsigned current = 256, alternate = 0, cls = 0;
    bool control = false, slave = false, ethernet = false, ncm = false;
    for (size_t at = 9; at < size;)
    {
        unsigned n = p[at];
        if (n < 2 || n > size - at)
            return -1;
        const uint8_t *d = p + at;
        if (d[1] == 4)
        {
            if (n < 9)
                return -1;
            current = d[2];
            alternate = d[3];
            cls = d[5];
            /* Match the portable SDK's first NCM control selection. A second
             * function cannot overwrite this binding or its DMA buffers. */
            if (!control && cls == 2 && d[6] == 13 && d[7] == 0 && !alternate)
            {
                f.control = current;
                control = true;
            }
        }
        else if (d[1] == 0x24 && control && current == f.control && !alternate)
        {
            if (n < 3)
                return -1;
            if (d[2] == 6)
            {
                if (n != 5 || d[3] != current)
                    return -1;
                f.data = d[4];
                slave = true;
            }
            else if (d[2] == 15)
            {
                if (n < 13)
                    return -1;
                f.mac_string = d[3];
                f.segment = get16(d + 8);
                ethernet = true;
            }
            else if (d[2] == 26)
            {
                if (n < 6)
                    return -1;
                f.capabilities = d[5];
                ncm = true;
            }
        }
        else if (d[1] == 5)
        {
            if (n < 7)
                return -1;
            if (control && current == f.control && (d[3] & 3) == 3 && (d[2] & 128))
                f.notification = d[2];
            if (slave && current == f.data && cls == 10 && alternate && (d[3] & 3) == 2)
            {
                if (!f.alternate)
                    f.alternate = alternate;
                if (alternate == f.alternate)
                {
                    if (d[2] & 128)
                    {
                        f.in = d[2];
                        f.in_mps = get16(d + 4);
                    }
                    else
                    {
                        f.out = d[2];
                        f.out_mps = get16(d + 4);
                    }
                }
            }
        }
        at += n;
    }
    if (!control || !slave || !ethernet || !ncm || !f.mac_string || f.segment < 1514 ||
        !f.in || !f.out || !f.in_mps || !f.out_mps || f.control == f.data)
        return -1;
    *out = f;
    return 0;
}

int carplay_ncm_parameters(const uint8_t *p, size_t size, struct carplay_ncm_limits *out)
{
    if (!p || !out || size < 28 || get16(p) < 28 || !(get16(p + 2) & 1))
        return -1;
    struct carplay_ncm_limits n = { get32(p + 4), get32(p + 16),
                                    get16(p + 20), get16(p + 22), get16(p + 24) };
    if (n.input < 2048 || n.output < 2048 || !n.divisor || n.remainder >= n.divisor ||
        n.alignment < 4 || (n.alignment & (n.alignment - 1)) || n.alignment > 4096 ||
        n.divisor > 4096)
        return -1;
    if (n.input > 16384)
        n.input = 16384;
    if (n.output > 8192)
        n.output = 8192;
    *out = n;
    return 0;
}

static int hex(unsigned c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}
int carplay_ncm_mac(const uint8_t *p, size_t size, uint8_t mac[6])
{
    if (!p || !mac || size < 26 || p[0] != 26 || p[1] != 3)
        return -1;
    unsigned any = 0;
    for (unsigned i = 0; i < 6; ++i)
    {
        int hi = hex(p[2 + 4 * i]), lo = hex(p[4 + 4 * i]);
        if (hi < 0 || lo < 0 || p[3 + 4 * i] || p[5 + 4 * i])
            return -1;
        mac[i] = (hi << 4) | lo;
        any |= mac[i];
    }
    return !any || (mac[0] & 1) ? -1 : 0;
}

int carplay_ncm_encode(const struct carplay_ncm_limits *n, uint16_t sequence,
                       const uint8_t *frame, size_t size, uint8_t *out, size_t cap)
{
    if (!n || !frame || !out || size < 14 || size > 1514 || !n->divisor ||
        n->remainder >= n->divisor || n->divisor > 4096 || n->alignment < 4 ||
        n->alignment > 4096 || (n->alignment & (n->alignment - 1)))
        return -1;
    /* NCM payload alignment applies to the network-layer header, after the
     * 14-byte Ethernet header (USB CDC-NCM 1.0, NTB parameter constraints). */
    size_t start = 12;
    start += (n->remainder + n->divisor - (start + 14) % n->divisor) % n->divisor;
    size_t table = (start + size + n->alignment - 1) & ~(size_t)(n->alignment - 1);
    size_t total = table + 16;
    if (total > cap || total > n->output || total > 65535)
        return -1;
    memset(out, 0, total);
    put32(out, 0x484d434e);
    put16(out + 4, 12);
    put16(out + 6, sequence);
    put16(out + 8, total);
    put16(out + 10, table);
    memcpy(out + start, frame, size);
    put32(out + table, 0x304d434e);
    put16(out + table + 4, 16);
    put16(out + table + 8, start);
    put16(out + table + 10, size);
    return (int)total;
}

/* Validate a complete NTB before exposing any Ethernet view to lwIP.
 * No Apple-specific fixed NDP offset/108-byte prefix assumption is valid on
 * this standard CDC-NCM function. Reject unsupported chains/CRC explicitly. */
int carplay_ncm_decode(const uint8_t *p, size_t size, carplay_ncm_frame_fn fn, void *arg)
{
    if (!p || !fn || size < 12 || size > 16384 || get32(p) != 0x484d434e || get16(p + 4) != 12)
        return -1;
    size_t block = get16(p + 8), table = get16(p + 10);
    if (block < 28 || block > size || size - block > 1 ||
        (size != block && p[block] != 0) || table < 12 || (table & 3) || table > block - 16)
        return -1;
    size_t length = get16(p + table + 4);
    if (get32(p + table) != 0x304d434e || get16(p + table + 6) ||
        length < 16 || (length & 3) || length > block - table)
        return -1;
    unsigned count = 0;
    bool terminated = false;
    for (size_t at = table + 8; at + 4 <= table + length; at += 4)
    {
        size_t start = get16(p + at), len = get16(p + at + 2);
        if (!start && !len)
        {
            terminated = true;
            break;
        }
        if (start < 12 || start > block || len < 14 || len > 1514 || len > block - start ||
            (start < table + length && table < start + len))
            return -1;
        for (unsigned i = 0; i < count; ++i)
        {
            size_t prev = get16(p + table + 8 + i * 4), plen = get16(p + table + 10 + i * 4);
            if (start < prev + plen && prev < start + len)
                return -1;
        }
        ++count;
        if (count > 64)
            return -1;
    }
    if (!terminated)
        return -1;
    for (unsigned i = 0; i < count; ++i)
    {
        size_t start = get16(p + table + 8 + i * 4), len = get16(p + table + 10 + i * 4);
        if (fn(arg, p + start, len))
            return -1;
    }
    return (int)count;
}
