#define _DEFAULT_SOURCE
#include "linux_demo.h"

#include <alsa/asoundlib.h>
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "airplay_audio_sender.h"
#include "audio_codec.h"
#include "audio_focus.h"
#include "audio_mixer.h"
#include "rtcp_linux.h"

enum { QUEUE_SIZE = 32, PAYLOAD_MAX = 4096, PERIOD_FRAMES = 480, PCM_SAMPLES = 11520 };
typedef struct
{
    rtcp_audio_format_t format;
    unsigned slot, generation;
    size_t size;
    uint8_t bytes[PAYLOAD_MAX];
} audio_packet_t;

typedef struct
{
    rtcp_audio_decoder_t *decoder;
    rtcp_audio_format_t format;
    unsigned generation;
} audio_lane_t;

static struct
{
    pthread_mutex_t mutex;
    pthread_t worker;
    snd_pcm_t *device;
    rtcp_audio_mixer_t *mixer;
    audio_packet_t queue[QUEUE_SIZE];
    unsigned first, count, generations[RTCP_AUDIO_MAX_STREAMS];
    unsigned gain;
    bool mute, duck, running;
    rtcp_audio_focus_t focus;
    atomic_bool stopping;
} player = { .mutex = PTHREAD_MUTEX_INITIALIZER, .gain = 32768 };

static struct
{
    const char *name;
    snd_pcm_t *device;
    airplay_audio_sender_t *sender;
    rtcp_audio_input_config_t config;
    pthread_t worker;
    int fd;
    atomic_bool stopping;
    bool running;
} microphone = { .fd = -1 };

static snd_pcm_t *open_pcm(const char *name, snd_pcm_stream_t direction, unsigned rate, unsigned channels)
{
    snd_pcm_t *pcm;
    if (snd_pcm_open(&pcm, name, direction, SND_PCM_NONBLOCK) < 0)
        return NULL;
    if (snd_pcm_set_params(pcm, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED,
                           channels, rate, 1, 40000) == 0)
        return pcm;
    snd_pcm_close(pcm);
    return NULL;
}

static int push(uint16_t type, const uint8_t *bytes, size_t size, const rtcp_audio_format_t *format,
                uint32_t timestamp, uint64_t arrival, void *context)
{
    (void)timestamp;
    (void)arrival;
    (void)context;
    unsigned slot = format->stream_slot_valid ? format->stream_slot :
        (type == RTCP_AUDIO_STREAM_ALT ? 1U : 0U);
    if (slot >= RTCP_AUDIO_MAX_STREAMS || !size || size > PAYLOAD_MAX)
        return RTCP_EINVAL;
    pthread_mutex_lock(&player.mutex);
    if (player.count < QUEUE_SIZE)
    {
        audio_packet_t *packet = &player.queue[(player.first + player.count++) % QUEUE_SIZE];
        packet->slot = slot;
        packet->generation = player.generations[slot];
        packet->format = *format;
        packet->size = size;
        memcpy(packet->bytes, bytes, size);
    }
    pthread_mutex_unlock(&player.mutex);
    return RTCP_OK;
}

static void stop_lane(unsigned slot, uint16_t type, void *context)
{
    (void)type;
    (void)context;
    if (slot < RTCP_AUDIO_MAX_STREAMS)
    {
        pthread_mutex_lock(&player.mutex);
        ++player.generations[slot];
        pthread_mutex_unlock(&player.mutex);
    }
}

static void update_modes(const rtcp_audio_modes_t *modes, void *context)
{
    (void)context;
    pthread_mutex_lock(&player.mutex);
    if (modes->has_call)
        player.focus.call_active = modes->call_active;
    if (modes->has_speech)
        player.focus.siri_active = modes->speech_mode == 1 || modes->speech_mode == 2;
    if (modes->has_main_audio)
        player.focus.remote_audio_entity = modes->main_audio_entity;
    pthread_mutex_unlock(&player.mutex);
}

static bool duck(bool active, float db, uint32_t duration, void *context)
{
    (void)db;
    (void)duration;
    (void)context;
    pthread_mutex_lock(&player.mutex);
    player.duck = active;
    pthread_mutex_unlock(&player.mutex);
    return true;
}

static void volume(float db, void *context)
{
    (void)context;
    if (!isfinite(db) || db > 0)
        return;
    pthread_mutex_lock(&player.mutex);
    player.mute = db <= -144;
    player.gain = (unsigned)(32768 * powf(10, db / 20));
    pthread_mutex_unlock(&player.mutex);
}

static void decode(audio_lane_t *lane, const audio_packet_t *packet, int16_t *pcm)
{
    const rtcp_audio_format_t *format = &packet->format;
    if (!lane->decoder || lane->format.sample_rate != format->sample_rate ||
        lane->format.channels != format->channels || lane->format.codec != format->codec)
    {
        rtcp_audio_decoder_destroy(lane->decoder);
        lane->decoder = NULL;
        rtcp_audio_mixer_flush(player.mixer, packet->slot);
        if (rtcp_audio_decoder_create(format, &lane->decoder) != RTCP_OK)
            return;
        lane->format = *format;
    }
    size_t samples = 0;
    if (rtcp_audio_decoder_decode(lane->decoder, packet->bytes, packet->size, pcm,
                                  PCM_SAMPLES, &samples) == RTCP_OK && samples)
    {
        rtcp_audio_mixer_push(player.mixer, packet->slot, pcm, samples / format->channels,
                              format->sample_rate, format->channels);
        rtcp_audio_mixer_clock(player.mixer, packet->slot, format->sample_rate / 25);
    }
}

static void *play(void *context)
{
    (void)context;
    int16_t decoded[PCM_SAMPLES], mixed[PERIOD_FRAMES * 2];
    audio_lane_t lanes[RTCP_AUDIO_MAX_STREAMS] = {0};
    size_t frames = 0, offset = 0;
    while (!atomic_load(&player.stopping))
    {
        audio_packet_t packet;
        pthread_mutex_lock(&player.mutex);
        bool have_packet = player.count != 0;
        if (have_packet)
        {
            packet = player.queue[player.first];
            player.first = (player.first + 1) % QUEUE_SIZE;
            --player.count;
        }
        unsigned generations[RTCP_AUDIO_MAX_STREAMS];
        memcpy(generations, player.generations, sizeof(generations));
        unsigned gain = player.gain;
        bool muted = player.mute, ducked = player.duck;
        rtcp_audio_focus_t focus = player.focus;
        pthread_mutex_unlock(&player.mutex);

        rtcp_audio_mixer_master(player.mixer, gain, muted);
        rtcp_audio_mixer_duck(player.mixer, ducked);
        rtcp_audio_focus_policy_t policy;
        rtcp_audio_focus_policy(&focus, &policy);
        for (unsigned slot = 0; slot < RTCP_AUDIO_MAX_STREAMS; ++slot)
        {
            if (lanes[slot].generation != generations[slot])
            {
                rtcp_audio_decoder_destroy(lanes[slot].decoder);
                lanes[slot] = (audio_lane_t){ .generation = generations[slot] };
                rtcp_audio_mixer_flush(player.mixer, slot);
            }
            unsigned lane_gain = slot == 2 ? policy.media_gain :
                (slot == 1 ? policy.alternate_gain : policy.main_gain);
            rtcp_audio_mixer_gain(player.mixer, slot, lane_gain, 0);
        }
        if (have_packet && packet.generation == generations[packet.slot])
            decode(&lanes[packet.slot], &packet, decoded);

        snd_pcm_sframes_t available = snd_pcm_avail_update(player.device);
        if (available < 0)
            snd_pcm_recover(player.device, (int)available, 1);
        if (available >= PERIOD_FRAMES)
        {
            if (offset == frames && rtcp_audio_mixer_buffered(player.mixer))
            {
                frames = rtcp_audio_mixer_render(player.mixer, mixed, PERIOD_FRAMES);
                offset = 0;
            }
            if (frames > offset)
            {
                snd_pcm_sframes_t written = snd_pcm_writei(player.device, mixed + offset * 2, frames - offset);
                if (written > 0)
                    offset += (size_t)written;
                else if (written < 0 && written != -EAGAIN)
                    snd_pcm_recover(player.device, (int)written, 1);
            }
        }
        if (!have_packet)
            rtcp_os_linux.sleep_ms(4);
    }
    for (unsigned slot = 0; slot < RTCP_AUDIO_MAX_STREAMS; ++slot)
        rtcp_audio_decoder_destroy(lanes[slot].decoder);
    return NULL;
}

static void *capture(void *context)
{
    (void)context;
    size_t frames = airplay_audio_sender_frames(microphone.sender);
    unsigned channels = microphone.config.sender.format.channels;
    int16_t samples[PCM_SAMPLES];
    uint8_t datagram[2048];
    size_t offset = 0;
    while (!atomic_load(&microphone.stopping))
    {
        snd_pcm_sframes_t got = snd_pcm_readi(microphone.device, samples + offset * channels, frames - offset);
        if (got < 0)
        {
            if (got == -EAGAIN)
                snd_pcm_wait(microphone.device, 20);
            else if (snd_pcm_recover(microphone.device, (int)got, 1) < 0)
                break;
            continue;
        }
        offset += (size_t)got;
        if (offset == frames)
        {
            int encoded = airplay_audio_sender_encode(microphone.sender, samples, frames, false,
                                                       datagram, sizeof(datagram));
            if (encoded > 0)
                microphone.config.net->udp_sendto(microphone.fd, &microphone.config.peer, datagram, (size_t)encoded);
            offset = 0;
        }
    }
    return NULL;
}

static void stop_input(void *context)
{
    (void)context;
    if (microphone.running)
    {
        atomic_store(&microphone.stopping, true);
        pthread_join(microphone.worker, NULL);
        microphone.running = false;
    }
    if (microphone.fd >= 0)
        microphone.config.net->close(microphone.fd);
    microphone.fd = -1;
    if (microphone.device)
        snd_pcm_close(microphone.device);
    microphone.device = NULL;
    airplay_audio_sender_destroy(microphone.sender);
    microphone.sender = NULL;
}

static bool start_input(const rtcp_audio_input_config_t *config, void *context)
{
    (void)context;
    stop_input(NULL);
    unsigned channels = config->sender.format.channels;
    if (!channels || airplay_audio_sender_create(&config->sender, &microphone.sender) != RTCP_OK)
        return false;
    microphone.config = *config;
    size_t frames = airplay_audio_sender_frames(microphone.sender);
    if (frames && frames <= PCM_SAMPLES / channels)
        microphone.device = open_pcm(microphone.name, SND_PCM_STREAM_CAPTURE,
                                      config->sender.format.sample_rate, channels);
    if (microphone.device)
        microphone.fd = config->net->udp_open(config->peer.family, 0, false);
    atomic_store(&microphone.stopping, false);
    if (microphone.fd < 0 || pthread_create(&microphone.worker, NULL, capture, NULL) != 0)
    {
        stop_input(NULL);
        return false;
    }
    microphone.running = true;
    return true;
}

void demo_audio_open(const demo_options_t *options, rtcp_media_backend_t *media,
                     bool *playback, bool *capture_ready)
{
    player.device = open_pcm(options->playback, SND_PCM_STREAM_PLAYBACK, RTCP_AUDIO_MIX_RATE, 2);
    player.mixer = player.device ? rtcp_audio_mixer_create(4096) : NULL;
    if (!player.mixer || pthread_create(&player.worker, NULL, play, NULL) != 0)
    {
        fprintf(stderr, "carplay: ALSA playback unavailable on %s\n", options->playback);
        return;
    }
    player.running = true;
    *playback = true;
    media->on_audio = push;
    media->on_audio_stop_ex = stop_lane;
    media->on_audio_modes = update_modes;
    media->on_audio_duck = duck;
    media->on_volume = volume;
    microphone.name = options->capture;
    snd_pcm_t *test = open_pcm(microphone.name, SND_PCM_STREAM_CAPTURE, 16000, 1);
    if (test)
    {
        snd_pcm_close(test);
        *capture_ready = true;
        media->on_audio_input_start = start_input;
        media->on_audio_input_stop = stop_input;
    }
}

void demo_audio_close(void)
{
    stop_input(NULL);
    if (player.running)
    {
        atomic_store(&player.stopping, true);
        pthread_join(player.worker, NULL);
    }
    if (player.device)
        snd_pcm_close(player.device);
    rtcp_audio_mixer_destroy(player.mixer);
}
