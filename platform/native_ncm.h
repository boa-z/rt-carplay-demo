/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CARPLAY_NATIVE_NCM_H
#define CARPLAY_NATIVE_NCM_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* USB CDC-NCM framing, distinct from Apple's vendor Ethernet RX format. */
struct carplay_ncm_function
{
    uint8_t control, data, alternate, mac_string, capabilities;
    uint8_t in, out, notification;
    uint16_t in_mps, out_mps, segment;
};
struct carplay_ncm_limits
{
    uint32_t input, output;
    uint16_t divisor, remainder, alignment;
};
int carplay_ncm_find(const uint8_t *, size_t, struct carplay_ncm_function *);
int carplay_ncm_parameters(const uint8_t *, size_t, struct carplay_ncm_limits *);
int carplay_ncm_mac(const uint8_t *, size_t, uint8_t[6]);
int carplay_ncm_encode(const struct carplay_ncm_limits *, uint16_t,
                       const uint8_t *, size_t, uint8_t *, size_t);
typedef int (*carplay_ncm_frame_fn)(void *, const uint8_t *, size_t);
int carplay_ncm_decode(const uint8_t *, size_t, carplay_ncm_frame_fn, void *);

/* Header metadata only: never retain TCP/UDP/media payloads. */
struct carplay_ncm_packet
{
    uint8_t source_mac[6], destination_mac[6];
    uint8_t source[16], destination[16], target[16];
    uint16_t ether_type, source_port, destination_port;
    uint8_t protocol, hop_limit, icmp_type, tcp_flags;
    bool neighbor, transport, fragmented;
};
/* 1 = IPv6, 0 = another EtherType, -1 = malformed/truncated header. */
int carplay_ncm_packet(const uint8_t *, size_t, struct carplay_ncm_packet *);

/* -1 until the native function has finished initialization. */
int carplay_native_ncm_interface(void);
void carplay_native_ncm_status(void);
#endif
