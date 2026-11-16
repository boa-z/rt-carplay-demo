/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Actual connection owner with deterministic transport/device boundaries.
 * Protocol and device workers have their own actual-source regressions. */
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include RTCP_CONNECTION_SOURCE

enum { USB_AFTER_IDLE, WIRELESS_RESERVATION, USB_RETRY, RADIO_FAILURE,
       ENUM_RETRY, NO_PICTURE, EVENT_LOSS, USB_START_FAILURE, NO_NETWORK,
       ATTACHED_TIMEOUT, USB_DISPLAY_SWITCH, WIRELESS_DISPLAY_SWITCH };
static unsigned scenario, now, boot, radio_at, pairs, opens, starts, closes;
static unsigned radio_starts, radio_stops, first_pair_at;
static unsigned network_samples;
static bool receiver, wired, radio, usb_session, seen_live;
static jmp_buf done;
#if defined(RTCARPLAY_SINGLE_SCREEN_FUSION)
static unsigned display_step, started_width;
static carplay_display_mode_t displayed_mode;
int carplay_fusion_set_mode(carplay_display_mode_t mode)
{ displayed_mode=mode; return RTCP_OK; }
int carplay_app_autostart_display(bool wired_mode, uint16_t width, uint16_t height)
{
    assert((width==768 || width==1024) && height==600);
    started_width=width;
    assert(wired_mode == !status.wireless);
    return wired_mode ? carplay_app_autostart_wired() : carplay_app_autostart();
}
#endif
static unsigned elapsed(void) { return now - boot; }
uint32_t rt_tick_get_millisecond(void) { return now; }
rt_mutex_t rt_mutex_create(const char *n, int f) { (void)n; (void)f; return (void *)1; }
int rt_mutex_take(rt_mutex_t m, int w) { assert(m); (void)w; return 0; }
int rt_mutex_release(rt_mutex_t m) { assert(m); return 0; }
int rt_kprintf(const char *f, ...) { (void)f; return 0; }
rt_thread_t rt_thread_create(const char *n, void (*f)(void *), void *a,
    unsigned s, unsigned p, unsigned t)
{ (void)n; (void)f; (void)a; (void)s; (void)p; (void)t; return (void *)2; }
int rt_thread_startup(rt_thread_t t) { (void)t; return 0; }
int rt_thread_delete(rt_thread_t t) { (void)t; return 0; }
static bool wireless_event(void)
{
    unsigned age = now - radio_at;
    return radio && ((scenario == WIRELESS_RESERVATION && age >= 800 && age < 1800)
#if defined(RTCARPLAY_SINGLE_SCREEN_FUSION)
        || (scenario==WIRELESS_DISPLAY_SWITCH && age>=800)
#endif
        );
}
void rt_thread_mdelay(int ms)
{
    assert(ms > 0); now += (unsigned)ms;
    assert(elapsed() < 200000); /* An owner that wedges fails the test. */
    if (status.stage == DEMO_LIVE) {
        seen_live = true;
#if defined(RTCARPLAY_SINGLE_SCREEN_FUSION)
        if (scenario==USB_DISPLAY_SWITCH || scenario==WIRELESS_DISPLAY_SWITCH) {
            if (!display_step) {
                assert(started_width==768);
                assert(carplay_connection_set_display_mode(CARPLAY_FULLSCREEN));
                assert(display_pending && displayed_mode==CARPLAY_LOCAL);
                display_step=1;
            } else if (display_step==1) {
                assert(started_width==1024 && closes && displayed_mode==CARPLAY_FULLSCREEN);
                assert(carplay_connection_set_display_mode(CARPLAY_FUSION));
                assert(display_pending && displayed_mode==CARPLAY_LOCAL);
                display_step=2;
            } else {
                assert(started_width==768 && starts>=3 && displayed_mode==CARPLAY_FUSION);
                demo_status_t sample;
                assert(carplay_connection_status(&sample) && !sample.display_reconfiguring);
                assert(sample.display_width==768 && sample.display_requested==CARPLAY_FUSION);
                assert(carplay_connection_set_display_mode(CARPLAY_LOCAL) && !display_pending);
                assert(displayed_mode==CARPLAY_LOCAL);
                longjmp(done,1);
            }
        }
#endif
        if (scenario == USB_RETRY || scenario == RADIO_FAILURE ||
            scenario == ENUM_RETRY || scenario == USB_START_FAILURE)
            longjmp(done, 1);
    }
}
int carplay_usb_open(void)
{
    ++opens;
    return scenario == ENUM_RETRY && opens < 3 ? RTCP_EIO : RTCP_OK;
}
bool carplay_usb_attached(void)
{
    if (scenario==WIRELESS_DISPLAY_SWITCH) return false;
    if (scenario == USB_AFTER_IDLE)
        return elapsed() >= 600 && !seen_live;
    if (scenario == WIRELESS_RESERVATION) return elapsed() >= 600;
    if (scenario == RADIO_FAILURE) return elapsed() >= 400;
    return true;
}
int carplay_usb_pair(void)
{
    assert(receiver && wired && !radio && !usb_session);
    assert(status.stage == DEMO_PAIR);
    ++pairs;
    if (pairs == 1) first_pair_at = now;
    if (scenario == WIRELESS_RESERVATION) {
        assert(elapsed() >= 1800 && radio_stops == 1 && seen_live);
        longjmp(done, 1); /* Wired contender could not steal the handoff. */
    }
    if (scenario == USB_RETRY && pairs == 1) return RTCP_EIO;
    if (scenario == USB_RETRY) assert(now - first_pair_at >= 5000);
    if (scenario == NO_PICTURE && pairs == 2) {
        assert(now - first_pair_at >= 95000);
        assert(status.last_error == RTCP_ETIMEDOUT);
        longjmp(done, 1);
    }
    if (scenario == ATTACHED_TIMEOUT && pairs == 2) {
        assert(now - first_pair_at >= 95000);
        assert(status.last_error == RTCP_ETIMEDOUT && !radio_starts);
        assert(closes && wireless_enabled && !status.wireless);
        longjmp(done, 1); /* No cable replug or wireless power cycle needed. */
    }
    if (scenario == EVENT_LOSS && pairs == 2) {
        assert(seen_live && closes && now - first_pair_at >= 1000);
        longjmp(done, 1); /* Reconnect while USB remains physically attached. */
    }
    if (scenario == NO_NETWORK && pairs == 2) {
        assert(!seen_live && closes && now - first_pair_at >= 15000);
        assert(network_samples == 4); /* initial, 5s, 10s, pre-cleanup */
        assert(status.last_error == RTCP_ETIMEDOUT);
        longjmp(done, 1);
    }
    usb_session = true;
    return RTCP_OK;
}
int carplay_usb_start_iap2(void) { assert(usb_session && !radio); return RTCP_OK; }
bool carplay_usb_iap2_running(void) { return usb_session; }
void carplay_usb_network_status(void)
{
    if (scenario == NO_NETWORK) {
        static const unsigned ages[] = {0, 5000, 10000, 15000};
        assert(network_samples < 4 && now - first_pair_at == ages[network_samples]);
        ++network_samples;
    }
}
int carplay_usb_close_session(void) { usb_session = false; ++closes; return RTCP_OK; }
int carplay_app_autostart(void)
{
    assert(!receiver && !usb_session && !radio);
    receiver = true; wired = false; ++starts;
    return RTCP_OK;
}
int carplay_app_autostart_wired(void)
{
    assert(!receiver && !usb_session && !radio);
    ++starts;
    if (scenario == USB_START_FAILURE && starts == 1) return RTCP_EIO;
    receiver = wired = true;
    return RTCP_OK;
}
bool carplay_app_is_running(void) { return receiver; }
int carplay_app_stop(void)
{
    assert(!radio && !usb_session); receiver = false; return RTCP_OK;
}
int carplay_iap2_start(void)
{
    assert(receiver && !wired && !usb_session && !radio);
    radio = true; radio_at = now; ++radio_starts;
    if (scenario == USB_AFTER_IDLE && radio_starts == 2) {
        assert(seen_live && pairs == 1 && radio_stops == 1);
        longjmp(done, 1);
    }
    return RTCP_OK;
}
int carplay_iap2_stop(void) { if (radio) ++radio_stops; radio = false; return RTCP_OK; }
void carplay_iap2_get_status(carplay_iap2_status_t *s)
{
    memset(s, 0, sizeof(*s)); s->running = radio;
    s->listening = radio && now - radio_at >= 200;
    if (scenario == RADIO_FAILURE && s->listening) s->error = RTCP_EIO;
    if (scenario == WIRELESS_RESERVATION) {
        unsigned age = now - radio_at;
        s->connected = age >= 400 && age < 800;
        s->authenticated = s->connected && age >= 600;
    }
}
int carplay_app_iap2_info(carplay_app_iap2_info_t *s)
{
    memset(s, 0, sizeof(*s)); strcpy(s->ssid, "rt-carplay-test");
    s->airplay_connected = wireless_event();
    return receiver && !wired ? RTCP_OK : RTCP_ESTATE;
}
int carplay_app_wired_info(carplay_app_wired_info_t *s)
{
    memset(s, 0, sizeof(*s)); strcpy(s->ipv6, "fe80::1234");
    return receiver && wired && scenario != NO_NETWORK ? RTCP_OK : RTCP_ESTATE;
}
int carplay_app_display_info(carplay_app_display_info_t *s)
{ memset(s, 0, sizeof(*s)); return RTCP_OK; }
int carplay_app_session_state(rtcp_receiver_state_t *s)
{
    memset(s, 0, sizeof(*s));
    s->running = receiver;
    s->transport_connected = s->authenticated = receiver && (usb_session || wireless_event());
    s->session_established = s->transport_connected;
    if (scenario == EVENT_LOSS && seen_live) s->session_established = false;
    return receiver ? RTCP_OK : RTCP_ESTATE;
}
void carplay_video_sink_stats(unsigned *n, unsigned *f, unsigned *d, unsigned *e)
{
    if (n) *n = 0;
    if (f) *f = (usb_session && scenario != NO_PICTURE) || wireless_event();
    if (d) *d = 0;
    if (e) *e = 0;
}
bool carplay_video_sink_has_presented(void)
{
    return (usb_session && scenario != NO_PICTURE && scenario != ATTACHED_TIMEOUT) || wireless_event();
}
int carplay_app_touch(uint8_t p, uint16_t i, uint16_t x, uint16_t y)
{ (void)p; (void)i; (void)x; (void)y; return RTCP_ESTATE; }
void carplay_app_performance_report(void) {}
void carplay_app_debug_counters(unsigned *r, unsigned *h, unsigned *e) { *r = *h = *e = 0; }
int carplay_app_pairing_storage_error(void) { return RTCP_OK; }
int carplay_usb_trust_cache_status(void) { return 2; }
int rtcp_bt_lubanlite_pairing_reply(uint32_t r, bool a) { (void)r; (void)a; return RTCP_OK; }
void rtcp_bt_lubanlite_pairing_status(rtcp_bt_pairing_status_t *s) { memset(s, 0, sizeof(*s)); }
int rtcp_bt_lubanlite_forget_bonds(void) { return RTCP_OK; }

int main(void)
{
    for (unsigned cycle = 0; cycle < 32; ++cycle) {
        for (scenario = USB_AFTER_IDLE; scenario <= WIRELESS_DISPLAY_SWITCH; ++scenario) {
#if !defined(RTCARPLAY_VIDEO)
            if (scenario == NO_PICTURE || scenario == ATTACHED_TIMEOUT) continue;
#endif
#if !defined(RTCARPLAY_SINGLE_SCREEN_FUSION)
            if (scenario>=USB_DISPLAY_SWITCH) continue;
#else
            display_step=started_width=0; requested_width=applied_width=768;
            requested_display=displayed_mode=CARPLAY_FUSION; display_pending=false;
#endif
            now = boot = cycle & 1 ? UINT32_MAX - 1000U : 0;
            pairs = opens = starts = closes = radio_starts = radio_stops = 0;
            network_samples = 0;
            receiver = radio = usb_session = seen_live = false;
            memset(&status, 0, sizeof(status));
            memset(failures, 0, sizeof(failures));
            memset(retry_waiting, 0, sizeof(retry_waiting));
            automatic = true;
            wireless_enabled = scenario != USB_RETRY && scenario != NO_PICTURE &&
                scenario != EVENT_LOSS && scenario != USB_START_FAILURE;
            requested_wireless = status.wireless = wireless_enabled;
            if (scenario == ATTACHED_TIMEOUT || scenario==USB_DISPLAY_SWITCH)
                requested_wireless = status.wireless = false;
            status_lock = (void *)1; manager_open = retry_requested = false;
            file_action = FILE_NONE;
            if (setjmp(done) == 0) {
                demo_worker(NULL);
                assert(!"connection owner unexpectedly returned");
            }
            if (scenario == ENUM_RETRY) assert(opens == 3 && elapsed() >= 10000);
            if (scenario == RADIO_FAILURE) assert(status.wireless_error == RTCP_EIO);
            if (scenario == USB_RETRY) assert(pairs == 2);
        }
    }
    memset(failures, 0, sizeof(failures));
    const unsigned delays[] = {5000, 10000, 20000, 40000, 60000, 60000};
    now = UINT32_MAX - 1000U;
    for (unsigned i = 0; i < sizeof(delays) / sizeof(delays[0]); ++i) {
        connection_finished(false, RTCP_EIO);
        assert(!retry_due(false));
        now += delays[i] - 1; assert(!retry_due(false));
        ++now; assert(retry_due(false));
    }
    status.stage = DEMO_LIVE;
    assert(!carplay_connection_set_wireless_enabled(false));
    status.stage = DEMO_WAIT; manager_open = true;
    assert(!carplay_connection_set_wireless_enabled(false));
    manager_open = false;
    assert(carplay_connection_set_wireless_enabled(false));
    assert(automatic && !wireless_enabled && retry_requested);
    assert(idle_actions() && !retry_requested && retry_due(false));
    assert(carplay_connection_set_wireless_enabled(true));
    assert(wireless_enabled);
#if defined(RTCARPLAY_SINGLE_SCREEN_FUSION)
    assert(!carplay_connection_set_display_mode((carplay_display_mode_t)99));
    manager_open=true;
    assert(!carplay_connection_set_display_mode(CARPLAY_FULLSCREEN));
    manager_open=false;
    assert(carplay_connection_set_display_mode(CARPLAY_FULLSCREEN) && display_pending);
    assert(carplay_connection_set_display_mode(CARPLAY_FUSION) && !display_pending);
    puts("Native display session changes: USB/wireless 768->1024->768, local preservation, blocked tools and coalesced requests passed (Host only)");
#endif
    puts("Actual automatic owner: simultaneous boot discovery, first-contender reservation, USB/radio failures, cooldown/retry, first-frame timeout, attached AirPlay loss, recovery and clock wrap passed over 32 cycles");
    return 0;
}
