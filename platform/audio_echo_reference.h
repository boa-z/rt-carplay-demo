/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* Application-owned, 48 kHz mono reference history. Caller serializes access.
 * Timestamps describe the first sample's estimated DAC presentation time,
 * rather than the write completion time. No network clock is involved. */
#define CARPLAY_ECHO_FRAMES 480U
#define CARPLAY_ECHO_BLOCKS 48U
typedef struct
{
    uint64_t at;
    bool valid;
    int16_t pcm[CARPLAY_ECHO_FRAMES];
} carplay_echo_block_t;
typedef struct
{
    carplay_echo_block_t blocks[CARPLAY_ECHO_BLOCKS];
    unsigned head;
} carplay_echo_history_t;

static inline void carplay_echo_push(carplay_echo_history_t *h,
                                     const int16_t *stereo, uint64_t at)
{
    carplay_echo_block_t *b = &h->blocks[h->head++ % CARPLAY_ECHO_BLOCKS];
    b->at = at;
    b->valid = true;
    for (unsigned i = 0; i < CARPLAY_ECHO_FRAMES; ++i)
        b->pcm[i] = (int16_t)(((int32_t)stereo[2 * i] + stereo[2 * i + 1]) / 2);
}

/* Stitch across block boundaries, preserving sub-frame alignment. Missing
 * samples produce a miss, never reuse an arbitrary stale reference frame. */
static inline bool carplay_echo_read(const carplay_echo_history_t *h,
                                     uint64_t at, int16_t *out)
{
    const carplay_echo_block_t *best = NULL;
    for (unsigned i = 0; i < CARPLAY_ECHO_FRAMES; ++i)
    {
        uint64_t t = at + (uint64_t)i * 1000000 / 48000;
        if (!best || t - best->at >= 10000)
        {
            best = NULL;
            for (unsigned j = 0; j < CARPLAY_ECHO_BLOCKS; ++j)
            {
                const carplay_echo_block_t *b = &h->blocks[j];
                if (b->valid && b->at <= t && t - b->at < 10000 && (!best || b->at > best->at))
                    best = b;
            }
        }
        if (!best)
            return false;
        /* Round to nearest sample: integer microseconds must not repeatedly
         * select the previous sample at 48 kHz. */
        size_t n = (size_t)(((t - best->at) * 48000 + 500000) / 1000000);
        if (n >= CARPLAY_ECHO_FRAMES)
            n = CARPLAY_ECHO_FRAMES - 1;
        out[i] = best->pcm[n];
    }
    return true;
}

/* Coarse acoustic/queue alignment, independent of the AEC filter. Only accept
 * strongly correlated far-end speech; double talk or silence must not move
 * the delay. Decimation bounds the work to 41 * 60 sample comparisons. */
static inline bool carplay_echo_delay(const carplay_echo_history_t *h,
                                      const int16_t *near, uint64_t end,
                                      unsigned *delay_ms)
{
    double best = 0.60;
    unsigned chosen = 0;
    bool found = false;
    for (unsigned delay = 0; delay <= 200; delay += 5)
    {
        uint64_t back = 10000U + (uint64_t)delay * 1000;
        if (end < back)
            continue;
        uint64_t start = end - back;
        double dot = 0, ne = 0, fe = 0, ns = 0, fs = 0;
        bool complete = true;
        const carplay_echo_block_t *block = NULL;
        for (unsigned i = 0; i < CARPLAY_ECHO_FRAMES; i += 8)
        {
            uint64_t t = start + (uint64_t)i * 1000000 / 48000;
            if (!block || t < block->at || t - block->at >= 10000)
            {
                block = NULL;
                for (unsigned j = 0; j < CARPLAY_ECHO_BLOCKS; ++j)
                {
                    const carplay_echo_block_t *b = &h->blocks[j];
                    if (b->valid && b->at <= t && t - b->at < 10000 &&
                        (!block || b->at > block->at))
                        block = b;
                }
            }
            if (!block)
            {
                complete = false;
                break;
            }
            size_t index = (size_t)(((t - block->at) * 48000 + 500000) / 1000000);
            if (index >= CARPLAY_ECHO_FRAMES)
                index = CARPLAY_ECHO_FRAMES - 1;
            double n = near[i], f = block->pcm[index];
            dot += n * f;
            ne += n * n;
            fe += f * f;
            ns += n;
            fs += f;
        }
        /* Microphone bias is not correlated speech. Remove DC before energy
         * and confidence tests, otherwise two constant offsets score 1. */
        dot -= ns * fs / 60;
        ne -= ns * ns / 60;
        fe -= fs * fs / 60;
        if (!complete || ne < 600000 || fe < 600000)
            continue;
        double score = dot * dot / (ne * fe);
        if (score > best)
        {
            best = score;
            chosen = delay;
            found = true;
        }
    }
    if (found)
        *delay_ms = chosen;
    return found;
}
