/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Actual sink + SDK public headers; model a two-slot MPP decoder. */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#ifndef VIDEO_SINK_SOURCE
#define VIDEO_SINK_SOURCE "../../platform/video_sink_lubanlite.c"
#endif
#include VIDEO_SINK_SOURCE

static unsigned slots, decoded, destroyed, delays;
static unsigned last_config_size;
static bool pressure;
static bool reference_mode, reference_active, fail_next;
static unsigned packet_type;
static unsigned render_remaining, render_id, returned[16], return_count;
static unsigned updates, vsyncs, displayed_id;
static bool hardware_reads, disable_queued;
static bool fail_present, fail_vsync, fail_disable, lifecycle, fail_startup, worker_pending;
static bool hold_worker, self_worker, reader_pending, check_display_reentry;
static unsigned visible_events, stopped_events, thread_deletes;
static uint8_t packet_data[1024];
static const uint8_t config[] = {0,0,0,1,0x67,0x42,0x00,0x1f,0x80,
                                0,0,0,1,0x68,0xce,0x38,0x80};
static const uint8_t frame[] = {0,0,0,1,0x65,0xb8,0x80};

rt_base_t rt_hw_interrupt_disable(void) { return 0; }
void rt_hw_interrupt_enable(rt_base_t level) { (void)level; }
uint32_t rt_tick_get_millisecond(void) { return 100; }
int rt_kprintf(const char *fmt, ...) { (void)fmt; return 0; }
int rt_mutex_take(rt_mutex_t m, int t) { (void)m; (void)t; return 0; }
int rt_mutex_release(rt_mutex_t m) { (void)m; return 0; }
int rt_sem_release(rt_sem_t s) { (void)s; return 0; }
int rt_sem_take(rt_sem_t s, int t) {
    (void)s; (void)t;
    if (s_sink.head == s_sink.tail) { s_sink.stop = true; return -1; }
    return 0;
}
void rt_thread_mdelay(int ms) {
    if (reader_pending) {
        assert(ms == 1 && atomic_load(&s_video_access) == (VIDEO_RETIRING | 1U));
        assert(s_sink.decoder != NULL && s_sink.lock != NULL);
        reader_pending = false;
        video_read_leave();
        return;
    }
    if (hold_worker) { assert(ms == 10 && s_sink.thread_running); return; }
    (void)ms; assert(++delays < 10);
    if (worker_pending) {
        worker_pending = false;
        assert(s_sink.stop && s_sink.decoder != NULL);
        sink_thread(&s_sink);
    }
}
int rt_device_control(rt_device_t d, int cmd, void *arg) {
    (void)d;
    /* The sink has no permission to read or modify the host UI layer. */
    assert((unsigned)cmd != AICFB_GET_ALPHA_CONFIG &&
           (unsigned)cmd != AICFB_UPDATE_ALPHA_CONFIG);
    if (cmd == AICFB_UPDATE_LAYER_CONFIG) {
        struct aicfb_layer_data *layer = arg;
        if ((fail_present && layer->enable) || (fail_disable && !layer->enable)) return -1;
        if (layer->enable) { ++updates; displayed_id = layer->buf.phy_addr[0]; hardware_reads=true; disable_queued=false; }
        else disable_queued=true;
    }
    if (cmd == AICFB_WAIT_FOR_VSYNC) {
        if (fail_vsync) return -1;
        ++vsyncs;
        if (disable_queued) { hardware_reads=false; disable_queued=false; }
    }
    if ((unsigned)cmd == AICFB_GET_SCREENINFO) {
        struct aicfb_screeninfo *screen = arg;
        screen->width = 1024; screen->height = 600;
    }
    return 0;
}
struct mpp_decoder *mpp_decoder_create(enum mpp_codec_type t) {
    (void)t; return (struct mpp_decoder *)(uintptr_t)1;
}
void mpp_decoder_destory(struct mpp_decoder *d) {
    (void)d; assert(!hardware_reads); ++destroyed; slots = 0; reference_active = false;
}
int mpp_decoder_init(struct mpp_decoder *d, struct decode_config *c) {
    (void)d; assert(c->packet_count == 2); return 0;
}
int mpp_decoder_get_packet(struct mpp_decoder *d, struct mpp_packet *p, int size) {
    (void)d;
    if (slots == 2) return DEC_NO_EMPTY_PACKET;
    assert(size > 0 && (size_t)size <= sizeof(packet_data));
    p->data = packet_data; return DEC_OK;
}
int mpp_decoder_put_packet(struct mpp_decoder *d, struct mpp_packet *p) {
    (void)d;
    assert(slots < 2); ++slots;
    const uint8_t *start = find_start_code(p->data, (uint8_t *)p->data + p->size);
    packet_type = start != NULL ? start[3] & 31U : 0U;
    if (p->size == (int)sizeof(config)) {
        last_config_size = (unsigned)p->size;
    }
    return DEC_OK;
}
int mpp_decoder_decode(struct mpp_decoder *d) {
    (void)d;
    if (!slots) return DEC_NO_READY_PACKET;
    --slots; ++decoded;
    if (fail_next) { fail_next = false; reference_active = false; return -1; }
    if (reference_mode) {
        if (packet_type == 5) reference_active = true;
        if (packet_type == 1) assert(reference_active);
    }
    /* First retry exercises rendering backpressure in pump_decoder(). */
    if (pressure) { pressure = false; return DEC_NO_EMPTY_FRAME; }
    return DEC_OK;
}
int mpp_decoder_get_frame(struct mpp_decoder *d, struct mpp_frame *f) {
    (void)d;
    if (!render_remaining) return DEC_NO_RENDER_FRAME;
    --render_remaining;
    memset(f, 0, sizeof(*f));
    f->id = (int)render_id++;
    f->buf.phy_addr[0] = (unsigned)f->id;
    f->buf.size.width = 1024;
    f->buf.size.height = 600;
    return DEC_OK;
}
int mpp_decoder_put_frame(struct mpp_decoder *d, struct mpp_frame *f) {
    (void)d;
    assert(return_count < 16);
    if (f->id == 99) assert(vsyncs == 1); /* old scanout held until swap */
    returned[return_count++] = (unsigned)f->id;
    return DEC_OK;
}

rt_device_t rt_device_find(const char *name) {
    assert(lifecycle && strcmp(name, "aicfb") == 0);
    return (rt_device_t)(uintptr_t)1;
}
rt_sem_t rt_sem_create(const char *n, unsigned v, int f) {
    (void)n; (void)v; (void)f; assert(lifecycle); return (rt_sem_t)(uintptr_t)1;
}
rt_mutex_t rt_mutex_create(const char *n, int f) {
    (void)n; (void)f; assert(lifecycle); return (rt_mutex_t)(uintptr_t)1;
}
int rt_sem_delete(rt_sem_t s) { (void)s; assert(lifecycle); return 0; }
int rt_mutex_delete(rt_mutex_t m) { (void)m; assert(lifecycle); return 0; }
rt_thread_t rt_thread_create(const char *n, void (*fn)(void *), void *arg,
                             unsigned stack, unsigned prio, unsigned tick) {
    (void)n; (void)stack; (void)prio; (void)tick;
    assert(lifecycle && fn == sink_thread && arg == &s_sink);
    return (rt_thread_t)(uintptr_t)1;
}
int rt_thread_startup(rt_thread_t t) {
    (void)t; assert(lifecycle && s_sink.thread_running);
    if (fail_startup) return -1;
    worker_pending = true; return 0;
}
int rt_thread_delete(rt_thread_t t) {
    (void)t; assert(lifecycle && fail_startup); ++thread_deletes; return 0;
}
rt_thread_t rt_thread_self(void) {
    return self_worker ? (rt_thread_t)(uintptr_t)1 : NULL;
}

static void display_event(bool active, void *context) {
    assert(context == &visible_events);
    if (active) {
        assert(vsyncs != 0 && !fail_present && !fail_vsync);
        ++visible_events;
    } else {
        assert(!s_sink.thread_running && !s_sink.running);
        assert(s_sink.decoder == NULL && s_sink.lock == NULL);
        ++stopped_events;
        if (check_display_reentry) {
            assert(carplay_video_sink_start(1024, 600) == RTCP_ESTATE);
            assert(carplay_video_sink_stop() == RTCP_ESTATE);
            assert(carplay_video_sink_set_display_observer(NULL, NULL) == RTCP_ESTATE);
        }
    }
}

static void reset(void) {
    memset(&s_sink, 0, sizeof(s_sink));
    s_sink.running = true;
    s_sink.fb = (rt_device_t)(uintptr_t)1;
    s_sink.lock = (rt_mutex_t)(uintptr_t)1;
    s_sink.decoder = (struct mpp_decoder *)(uintptr_t)1;
    s_sink.panel_width = s_sink.display_width = 1024;
    s_sink.panel_height = s_sink.display_height = 600;
    slots = decoded = destroyed = delays = last_config_size = 0;
    pressure = false; hardware_reads=false; disable_queued=false;
    reference_mode = reference_active = fail_next = false;
    render_remaining = return_count = updates = vsyncs = displayed_id = 0;
    render_id = 1;
    fail_disable = fail_present = fail_vsync = lifecycle = fail_startup = worker_pending = false;
    visible_events = stopped_events = thread_deletes = 0;
    s_display_observer = NULL; s_display_context = NULL;
}
int main(int argc, char **argv) {
#if defined(RTCARPLAY_SINGLE_SCREEN_FUSION)
    assert(carplay_fusion_init(1024,600) == RTCP_OK);
    reset();
    render_remaining=1;
    pump_decoder(&s_sink);
    carplay_fusion_snapshot_t layout;
    carplay_fusion_snapshot(100,&layout);
    assert(layout.video_active && layout.video_rect.width == 768 && layout.video_rect.height == 450);
    assert(atomic_load(&s_sink.latched_frames) == 1);
    unsigned before_updates=updates;
    assert(carplay_fusion_set_mode(CARPLAY_FULLSCREEN) == RTCP_OK);
    fail_vsync=true;
    apply_display_mode(&s_sink);
    carplay_fusion_snapshot(100,&layout);
    assert(layout.video_rect.width == 768 && layout.generation != layout.video_generation);
    fail_vsync=false;
    apply_display_mode(&s_sink);
    carplay_fusion_snapshot(100,&layout);
    assert(layout.video_rect.width == 1024 && layout.video_rect.height == 600);
    assert(layout.generation == layout.video_generation && updates == before_updates+2);
    assert(return_count == 0 && atomic_load(&s_sink.latched_frames) == 1);
    apply_display_mode(&s_sink);
    assert(updates == before_updates+2); /* No redundant static-frame update. */
    assert(retire_scanout(&s_sink));
    assert(return_count == 1);
    assert(carplay_fusion_set_mode(CARPLAY_FUSION) == RTCP_OK);
#endif
    if (argc > 1 && strcmp(argv[1], "queue") == 0) {
        reset();
        for (unsigned i = 0; i < 10; ++i)
            carplay_video_sink_frame(false, frame, sizeof(frame), 0, NULL);
        sink_thread(&s_sink);
        assert(decoded == 10 && s_sink.nal_count == 10 && delays == 0);
        puts("bounded packet queue progresses without waiting");
        return 0;
    }
    reset();
    s_sink.frame_count = 8; /* configuration after an established stream */
    carplay_video_sink_frame(true, config, sizeof(config), 0, NULL);
    assert(carplay_video_sink_pending() == 1);
    for (unsigned i = 0; i < 10; ++i)
        carplay_video_sink_frame(false, frame, sizeof(frame), 0, NULL);
    sink_thread(&s_sink);
    assert(destroyed == 1 && s_sink.decoder_resets == 1);
    assert(last_config_size == sizeof(config));
    assert(decoded == 11 && s_sink.nal_count == 11 && slots == 0);
    assert(s_sink.decode_errors == 0 && delays == 0);

    reset(); /* Repeated SPS/PPS must preserve an established reference chain. */
    reference_mode = true;
    render_remaining = 1; /* establish frame_count before repeated config */
    uint8_t inter[] = {0,0,0,1,0x41,0xb8,0x80};
    carplay_video_sink_frame(true, config, sizeof(config), 0, NULL);
    carplay_video_sink_frame(false, frame, sizeof(frame), 0, NULL);
    carplay_video_sink_frame(true, config, sizeof(config), 0, NULL);
    carplay_video_sink_frame(false, inter, sizeof(inter), 0, NULL);
    sink_thread(&s_sink);
    assert(destroyed == 0 && decoded == 3 && reference_active);
    assert(s_sink.repeated_configs == 1 && s_sink.decode_errors == 0);

    reset(); /* Changed SPS/PPS: skip dependent picture, replay config at IDR. */
    reference_mode = true;
    uint8_t changed[sizeof(config)];
    memcpy(changed, config, sizeof(config)); changed[7] ^= 1;
    carplay_video_sink_frame(true, config, sizeof(config), 0, NULL);
    carplay_video_sink_frame(false, frame, sizeof(frame), 0, NULL);
    carplay_video_sink_frame(true, changed, sizeof(changed), 0, NULL);
    carplay_video_sink_frame(false, inter, sizeof(inter), 0, NULL);
    carplay_video_sink_frame(false, frame, sizeof(frame), 0, NULL);
    carplay_video_sink_frame(false, inter, sizeof(inter), 0, NULL);
    sink_thread(&s_sink);
    assert(destroyed == 1 && decoded == 5 && s_sink.waiting_nals == 1);
    assert(reference_active && s_sink.decode_errors == 0);

    reset(); /* Poisoned decoder is quarantined until cached config + IDR. */
    reference_mode = true;
    nal_entry_t cfg = {.data = (uint8_t *)config, .len = sizeof(config), .configure = true};
    assert(accept_configuration(&s_sink, &cfg));
    assert(submit_nal(&s_sink, &cfg) == 0); pump_decoder(&s_sink);
    nal_entry_t idr = {.data = (uint8_t *)frame, .len = sizeof(frame)};
    assert(prepare_picture(&s_sink, &idr));
    assert(submit_nal(&s_sink, &idr) == 0); fail_next = true; pump_decoder(&s_sink);
    assert(s_sink.wait_idr && s_sink.recovery_pending);
    carplay_video_sink_frame(false, inter, sizeof(inter), 0, NULL);
    carplay_video_sink_frame(false, frame, sizeof(frame), 0, NULL);
    carplay_video_sink_frame(false, inter, sizeof(inter), 0, NULL);
    sink_thread(&s_sink);
    assert(destroyed == 1 && decoded == 5 && s_sink.waiting_nals == 1);
    assert(reference_active && s_sink.decode_errors == 1);

    reset(); /* Input loss invalidates only entries following the gap. */
    reference_mode = true;
    uint8_t later_idr_slice[] = {0,0,0,1,0x65,0x40,0x80};
    carplay_video_sink_frame(true, config, sizeof(config), 0, NULL);
    carplay_video_sink_frame(false, frame, sizeof(frame), 0, NULL);
    carplay_video_sink_frame(false, inter, sizeof(inter), 0, NULL);
    drop_nal(&s_sink); /* next queued packet belongs after this missing input */
    carplay_video_sink_frame(false, inter, sizeof(inter), 0, NULL);
    carplay_video_sink_frame(false, later_idr_slice, sizeof(later_idr_slice), 0, NULL);
    carplay_video_sink_frame(false, frame, sizeof(frame), 0, NULL);
    carplay_video_sink_frame(false, inter, sizeof(inter), 0, NULL);
    sink_thread(&s_sink);
    assert(destroyed == 1 && decoded == 6 && s_sink.waiting_nals == 2);
    assert(reference_active && s_sink.decode_errors == 0);

    reset(); /* retry must decode to free slots on this same thread */
    slots = 2; pressure = true;
    nal_entry_t nal = {.data = (uint8_t *)frame, .len = sizeof(frame)};
    assert(submit_nal(&s_sink, &nal) == 0);
    pump_decoder(&s_sink);
    assert(decoded == 3 && slots == 0 && delays == 1);

    reset(); /* full queue must not admit only half a configuration */
    for (unsigned i = 0; i < SINK_QUEUE_LENGTH - 1; ++i)
        carplay_video_sink_frame(false, frame, sizeof(frame), 0, NULL);
    carplay_video_sink_frame(true, config, sizeof(config), 0, NULL);
    assert(s_sink.dropped_nals == 1);
    sink_thread(&s_sink);
    assert(destroyed == 0 && decoded == SINK_QUEUE_LENGTH - 1);
    reset(); /* Coalesce decoded backlog, preserve hardware scanout lifetime. */
    s_sink.have_frame = true;
    s_sink.held.id = 99;
    render_remaining = 3;
    assert(!s_sink.display_reported);
    assert(drain_frames(&s_sink) == 3);
    assert(updates == 1 && vsyncs == 1 && displayed_id == 3);
    assert(s_sink.display_reported && carplay_video_sink_has_presented()); /* no built-in UI policy */
    assert(return_count == 3 && returned[0] == 1 && returned[1] == 2 && returned[2] == 99);
    assert(s_sink.frame_count == 1 && s_sink.skipped_frames == 2 && s_sink.held.id == 3);

    reset(); /* Failed present/vsync cannot transfer HMI ownership. */
    s_display_observer = display_event; s_display_context = &visible_events;
    render_remaining = 1; fail_present = true;
    assert(drain_frames(&s_sink) == 1 && visible_events == 0);
    assert(s_sink.frame_count > 0 && !carplay_video_sink_has_presented());
    render_remaining = 1; fail_present = false; fail_vsync = true;
    assert(drain_frames(&s_sink) == 0 && visible_events == 0);
    assert(render_remaining == 1 && return_count == 0 && s_sink.have_pending_scanout);
    assert(s_sink.frame_count > 0 && !carplay_video_sink_has_presented());
    render_remaining = 1; fail_vsync = false;
    assert(drain_frames(&s_sink) == 1 && visible_events == 1);
    assert(carplay_video_sink_has_presented());
    render_remaining = 1;
    assert(drain_frames(&s_sink) == 1 && visible_events == 1);
    assert(carplay_video_sink_has_presented());
    assert(carplay_video_sink_set_display_observer(NULL, NULL) == RTCP_ESTATE);

    reset(); /* Failed swap keeps both scanouts until the same update latches. */
    s_sink.have_frame=true; s_sink.held.id=88;
    render_remaining=1; fail_vsync=true;
    assert(drain_frames(&s_sink) == 1 && return_count == 0);
    assert(s_sink.held.id == 88 && s_sink.have_pending_scanout);
    assert(drain_frames(&s_sink) == 0 && return_count == 0);
    fail_vsync=false;
    assert(drain_frames(&s_sink) == 0 && return_count == 1 && returned[0] == 88);
    assert(s_sink.have_frame && !s_sink.have_pending_scanout && s_sink.held.id == 1);
    /* Reset may not destroy a decoder still read by the DE. */
    fail_vsync=true;
    assert(!recreate_decoder(&s_sink) && !destroyed && return_count == 1);
    assert(s_sink.have_frame && s_sink.held.id == 1);
    fail_vsync=false;
    assert(recreate_decoder(&s_sink) && destroyed == 1 && return_count == 2);

    reset(); /* Immediate stop must join a worker not yet scheduled. */
    memset(&s_sink, 0, sizeof(s_sink)); lifecycle = true;
    assert(carplay_video_sink_set_display_observer(display_event, &visible_events) == RTCP_OK);
    assert(carplay_video_sink_start(1024, 600) == RTCP_OK);
    assert(worker_pending && s_sink.thread_running);
    assert(carplay_video_sink_stop() == RTCP_OK);
    assert(!worker_pending && stopped_events == 1 && destroyed == 1);
    assert(carplay_video_sink_stop() == RTCP_OK);
    assert(stopped_events == 1);
    fail_startup = true;
    assert(carplay_video_sink_start(1024, 600) == RTCP_EIO);
    assert(thread_deletes == 1 && destroyed == 2);
    assert(!s_sink.running && !s_sink.thread_running && s_sink.lock == NULL);
    assert(stopped_events == 1 && carplay_video_sink_set_display_observer(NULL, NULL) == RTCP_OK);
    fail_startup = false;
    assert(carplay_video_sink_set_display_observer(display_event, &visible_events) == RTCP_OK);
    assert(carplay_video_sink_start(1024, 600) == RTCP_OK);
    self_worker = true;
    assert(carplay_video_sink_stop() == RTCP_ESTATE);
    assert(s_sink.running && !s_sink.stop); self_worker = false;
    unsigned before_destroy = destroyed, before_stopped = stopped_events;
    hold_worker = true;
    assert(carplay_video_sink_stop() == RTCP_ETIMEDOUT);
    assert(!s_sink.running && s_sink.thread_running && s_sink.decoder && s_sink.lock && s_sink.wake);
    assert(destroyed == before_destroy && stopped_events == before_stopped);
    assert(carplay_video_sink_start(1024, 600) == RTCP_ESTATE);
    assert(carplay_video_sink_set_display_observer(NULL, NULL) == RTCP_ESTATE);
    hold_worker = false;worker_pending = false;
    sink_thread(&s_sink); /* late retirement after timeout */
    assert(carplay_video_sink_start(1024, 600) == RTCP_ESTATE);
    check_display_reentry = true;
    assert(carplay_video_sink_stop() == RTCP_OK);
    assert(destroyed == before_destroy + 1 && stopped_events == before_stopped + 1);
    assert(carplay_video_sink_stop() == RTCP_OK);

    delays = 0;
    assert(carplay_video_sink_start(1024,600) == RTCP_OK);
    s_sink.have_frame=true; s_sink.held.id=88;
    render_remaining=1; fail_vsync=true;
    assert(drain_frames(&s_sink) == 1 && s_sink.have_pending_scanout);
    unsigned held_returns=return_count, held_destroyed=destroyed, held_stopped=stopped_events;
    assert(carplay_video_sink_stop() == RTCP_EIO);
    assert(!s_sink.thread_running && s_sink.decoder && s_sink.lock && s_sink.wake);
    assert(s_sink.have_frame && s_sink.have_pending_scanout && return_count == held_returns);
    assert(destroyed == held_destroyed && stopped_events == held_stopped);
    assert(carplay_video_sink_start(1024,600) == RTCP_ESTATE);
    assert(carplay_video_sink_set_display_observer(NULL,NULL) == RTCP_ESTATE);
    fail_vsync=false; fail_disable=true;
    assert(carplay_video_sink_stop() == RTCP_EIO && return_count == held_returns && destroyed == held_destroyed);
    fail_disable=false;
    assert(carplay_video_sink_stop() == RTCP_OK);
    assert(return_count == held_returns+2 && destroyed == held_destroyed+1 && stopped_events == held_stopped+1);

    delays = 0;
    assert(carplay_video_sink_start(1024, 600) == RTCP_OK);
    assert(video_read_enter());reader_pending = true;
    assert(carplay_video_sink_stop() == RTCP_OK); /* retirement drains accepted reader */
    assert(!reader_pending && atomic_load(&s_video_access) == 0U);
    atomic_fetch_or(&s_video_access, VIDEO_RETIRING);
    unsigned n = 1, f = 1, d = 1, e = 1;
    carplay_video_sink_stats(&n, &f, &d, &e);
    carplay_video_perf_t perf;memset(&perf, 0xff, sizeof(perf));
    carplay_video_sink_perf(&perf);
    assert(!n && !f && !d && !e && !perf.frames && !perf.packets);
    assert(!carplay_video_sink_is_running() && !carplay_video_sink_pending());
    unsigned head = s_sink.head;
    carplay_video_sink_frame(false, frame, sizeof(frame), 0, NULL);
    assert(head == s_sink.head);
    char *status_args[] = {"carplay_video", "status"};
    assert(cmd_carplay_video(2, status_args) == RTCP_ESTATE);
    atomic_store(&s_video_access, 0U);
    assert(!atomic_flag_test_and_set(&s_video_shell));
    assert(cmd_carplay_video(2, status_args) == RTCP_ESTATE);
    atomic_flag_clear(&s_video_shell);
#ifdef RTCARPLAY_DEMO_LAUNCHER
    char *stop_args[] = {"carplay_video", "stop"};
    char *start_args[] = {"carplay_video", "start"};
    char *play_args[] = {"carplay_video", "play", "unused"};
    assert(cmd_carplay_video(2, stop_args) == RTCP_ESTATE);
    assert(cmd_carplay_video(2, start_args) == RTCP_ESTATE);
    assert(cmd_carplay_video(3, play_args) == RTCP_ESTATE);
#endif
    assert(cmd_carplay_video(2, status_args) == RTCP_OK);
    for (unsigned cycle = 0; cycle < 32; ++cycle) {
        delays = 0;
        assert(carplay_video_sink_start(1024, 600) == RTCP_OK);
        assert(carplay_video_sink_stop() == RTCP_OK);
    }
    assert(carplay_video_sink_set_display_observer(NULL, NULL) == RTCP_OK);
    puts("Luban-Lite actual video sink regression passed");
    puts("Video lifecycle: self-join, retained timeout/late cleanup, reader retirement, command exclusion and 32 restarts passed");
    return 0;
}
