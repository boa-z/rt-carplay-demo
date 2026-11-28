#ifndef CARPLAY_LINUX_DEMO_H
#define CARPLAY_LINUX_DEMO_H

#include <stdbool.h>
#include <stdint.h>
#include <signal.h>
#include <net/if.h>

#include "airplay_ed25519.h"
#include "iap2_session.h"
#include "phone_track.h"
#include "rtcp/mfi.h"
#include "rtcp/receiver.h"

typedef enum
{
    DEMO_MFI_DEFAULT,
    DEMO_MFI_HARDWARE,
    DEMO_MFI_SOFTWARE,
    DEMO_MFI_AUTO,
} demo_mfi_mode_t;

typedef struct demo_mfi demo_mfi_t;

typedef struct
{
    const char *directory;
    const char *interface;
    const char *ap_config;
    const char *adapter;
    const char *framebuffer;
    const char *touch;
    const char *i2c;
    const char *mfi_cert;
    const char *mfi_key;
    demo_mfi_mode_t mfi_mode;
    uint8_t mfi_protocol;
    const rtcp_mfi_provider_t *mfi_provider;
    const char *gnss;
    const char *playback;
    const char *capture;
    uint16_t width;
    uint16_t height;
    uint8_t fps;
    bool fps_override;
    uint16_t port;
    bool wired;
    bool wireless;
    bool headless;
} demo_options_t;

typedef struct
{
    airplay_ed25519_keypair_t keys;
    char device_id[18];
    char public_key[65];
    char store[256];
} demo_identity_t;

typedef struct
{
    char carrier[65];
    uint16_t battery;
    uint8_t signal, charging, call;
    bool has_battery, has_signal, has_charging, has_call, airplane;
} demo_phone_status_t;

typedef struct
{
    char road[129], destination[129], distance[65];
} demo_route_t;

typedef struct
{
    char interface[IF_NAMESIZE];
    char address[64];
    uint64_t generation;
    uint16_t port;
    bool transport;
    bool event;
} demo_endpoint_t;

typedef struct
{
    bool subscribed;
    size_t length;
    char sentence[RTCP_LOCATION_NMEA_MAX];
} demo_gnss_stream_t;

extern volatile sig_atomic_t demo_stop;
void demo_boot_mark(const char *stage);
int demo_identity_open(demo_identity_t *identity, const char *directory,
                       rtcp_pairing_provider_t *pairing);
int demo_mfi_open(const demo_options_t *options, demo_mfi_t **out);
const rtcp_mfi_provider_t *demo_mfi_provider(const demo_mfi_t *mfi);
void demo_mfi_close(demo_mfi_t *mfi);
void demo_track_update(const rtcp_now_playing_details_t *update, void *context);
void demo_phone_update(const rtcp_phone_status_t *update, void *context);
void demo_route_update(const rtcp_route_details_t *update, void *context);
void demo_phone_snapshot(carplay_phone_track_t *track, demo_phone_status_t *phone, demo_route_t *route);
bool demo_gnss_open(const char *device);
void demo_gnss_close(void);
bool demo_gnss_available(void);
void demo_gnss_tick(demo_gnss_stream_t *stream, iap2_session_t *session, uint64_t now);

#ifdef CARPLAY_USB
int demo_wired_start(const demo_options_t *options, const demo_identity_t *identity);
void demo_wired_stop(void);
void demo_wired_snapshot(demo_endpoint_t *endpoint);
void demo_wired_reconnect(uint64_t generation);
void demo_wired_publish(uint64_t generation, uint16_t port, const rtcp_receiver_state_t *state);
#endif
#ifdef CARPLAY_BLUEZ
int demo_wireless_configure(const demo_options_t *options, const demo_identity_t *identity);
void demo_wireless_step(rtcp_receiver_t *receiver, const char *ip, bool active);
void demo_wireless_backend(rtcp_iap2_backend_t *backend);
void demo_wireless_close(void);
int demo_wireless_passkey(void);
void demo_wireless_confirm(bool accept);
void demo_wireless_discoverable(void);
#endif
#ifdef CARPLAY_ALSA
void demo_audio_open(const demo_options_t *options, rtcp_media_backend_t *media,
                     bool *playback, bool *capture);
void demo_audio_close(void);
void demo_audio_set_local_volume(unsigned percent);
void demo_audio_set_mic_muted(bool muted);
#endif
#ifdef CARPLAY_AIC_MPP
int demo_video_open(const demo_options_t *options, rtcp_media_backend_t *media);
void demo_video_close(void);
bool demo_video_needs_keyframe(void);
bool demo_video_visible(void);
void demo_video_session(bool live);
#endif
#ifdef CARPLAY_LVGL
typedef struct
{
    int (*set_projection)(bool enabled, void *context);
    void (*request_keyframe)(void *context);
    void (*send_touch)(const rtcp_touch_contact_t *contacts, void *context);
    int (*set_appearance)(rtcp_appearance_setting_t appearance, void *context);
    void *context;
} demo_launcher_commands_t;

typedef struct
{
    rtcp_receiver_state_t receiver;
    rtcp_projection_owner_t projection_owner;
    uint32_t stream_generation;
    uint64_t receiver_generation;
    bool wired;
    bool projection_visible;
} demo_launcher_snapshot_t;

int demo_launcher_open(demo_options_t *options);
bool demo_launcher_show_local(void *context);
void demo_launcher_attach_media(rtcp_media_backend_t *media);
uint16_t demo_launcher_projection_width(void);
void demo_launcher_apply_layout(void);
void demo_launcher_step(const demo_launcher_commands_t *commands,
                        const demo_launcher_snapshot_t *snapshot);
void demo_launcher_close(void);
#endif

#endif
