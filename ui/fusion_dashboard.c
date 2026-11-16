/* SPDX-License-Identifier: GPL-3.0-or-later */
#if defined(RTCARPLAY_SINGLE_SCREEN_FUSION)
#include "fusion_dashboard.h"
#include "fusion_display.h"
#include "video_layer_policy.h"
#include "video_sink_lubanlite.h"
#include "rtcp/rtcp.h"
#include <rtthread.h>
#include <stdio.h>
#include <string.h>
#include "ui_language.h"
#define lv_label_set_text carplay_ui_set_text
#define TR carplay_ui_translate
#if defined(LPKG_USING_CPU_USAGE)
#include "cpu_usage.h"
#endif

/* These built-in LVGL fonts are compiled by the application when the board
 * lv_conf.h leaves them disabled despite launcher Kconfig selections. */
LV_FONT_DECLARE(lv_font_montserrat_24);
LV_FONT_DECLARE(lv_font_montserrat_32);
LV_FONT_DECLARE(carplay_cjk_24);

static lv_obj_t *dashboard, *launcher_screen, *background;
static lv_obj_t *title, *source_label, *speed, *gear, *battery, *connection, *metrics;
static lv_obj_t *view_button, *connections_button, *menu_button;
static bool showing_launcher;
static int arranged_mode=-1;
static uint32_t stats_ms;
static unsigned stats_frames, rate_tenths;

static void text(lv_obj_t *obj, const char *value)
{ if (strcmp(lv_label_get_text(obj),TR(value))) lv_label_set_text(obj,value); }

static lv_obj_t *make_label(lv_obj_t *parent, uint32_t color, bool large)
{
    lv_obj_t *obj=lv_label_create(parent);
    lv_obj_set_style_text_color(obj,lv_color_hex(color),0);
    if (large) lv_obj_set_style_text_font(obj,&lv_font_montserrat_32,0);
    return obj;
}

static void show_dashboard(lv_event_t *event)
{
    (void)event; carplay_fusion_ui_resume();
}
void carplay_fusion_ui_show_launcher(void)
{
    showing_launcher=true;
    (void)carplay_fusion_set_mode(CARPLAY_LOCAL);
    if (lv_screen_active()!=launcher_screen) lv_screen_load(launcher_screen);
}
void carplay_fusion_ui_resume(void)
{
    demo_status_t status={0};
    showing_launcher=false;
    if (carplay_connection_status(&status))
        (void)carplay_fusion_set_mode(status.display_reconfiguring ? CARPLAY_LOCAL : status.display_requested);
}
static void show_connections(lv_event_t *event)
{ (void)event; carplay_fusion_ui_show_launcher(); }
static void open_display(lv_event_t *event)
{ (void)event; carplay_launcher_open_display_settings(); }

static lv_obj_t *make_button(lv_obj_t *parent, const char *caption, lv_event_cb_t callback)
{
    lv_obj_t *obj=lv_button_create(parent);
    lv_obj_set_style_bg_color(obj,lv_color_hex(0x20394F),0);
    lv_obj_set_style_radius(obj,10,0);
    lv_obj_set_size(obj,220,42);
    lv_obj_add_event_cb(obj,callback,LV_EVENT_CLICKED,NULL);
    lv_obj_t *label=lv_label_create(obj); lv_label_set_text(label,caption); lv_obj_center(label);
    return obj;
}

static void place(lv_obj_t *obj, int x, int y, int width)
{ lv_obj_set_pos(obj,x,y); lv_obj_set_width(obj,width); }

static void arrange(const carplay_fusion_snapshot_t *s)
{
    if (arranged_mode == (int)s->effective) return;
    arranged_mode=s->effective;
    bool fusion=s->effective == CARPLAY_FUSION;
    bool fullscreen=s->effective == CARPLAY_FULLSCREEN;
    lv_obj_t *instruments[]={background,title,source_label,speed,gear,battery,connection,metrics,view_button,connections_button};
    for (unsigned i=0;i<sizeof(instruments)/sizeof(instruments[0]);++i) {
        if (fullscreen) lv_obj_add_flag(instruments[i],LV_OBJ_FLAG_HIDDEN);
        else lv_obj_remove_flag(instruments[i],LV_OBJ_FLAG_HIDDEN);
    }
    if (fullscreen) lv_obj_remove_flag(menu_button,LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(menu_button,LV_OBJ_FLAG_HIDDEN);
    if (fullscreen) return;
    int x=fusion ? s->width-s->local_width+16 : 40;
    int w=fusion ? s->local_width-32 : s->width-80;
    lv_obj_set_pos(background,fusion ? s->width-s->local_width : 16,fusion ? 0 : 96);
    lv_obj_set_size(background,fusion ? s->local_width : s->width-32,fusion ? s->height : s->height-192);
    place(title,x,24,w); place(source_label,x,64,w);
    place(speed,x,112,fusion ? w : 310);
    place(gear,fusion ? x : 390,fusion ? 188 : 120,fusion ? w : 240);
    place(battery,fusion ? x : 680,fusion ? 242 : 120,fusion ? w : 280);
    place(connection,x,fusion ? 304 : 236,w);
    place(metrics,x,fusion ? 370 : 344,w);
    lv_obj_set_style_text_font(gear,carplay_ui_is_chinese() ? &carplay_cjk_24 : fusion ? &lv_font_montserrat_24 : &lv_font_montserrat_32,0);
    lv_obj_set_style_text_font(battery,carplay_ui_is_chinese() ? &carplay_cjk_24 : fusion ? &lv_font_montserrat_24 : &lv_font_montserrat_32,0);
    lv_obj_set_style_text_font(metrics,&lv_font_montserrat_12,0);
    lv_obj_set_pos(view_button,x,fusion ? s->height-108 : s->height-68);
    lv_obj_set_pos(connections_button,fusion ? x : x+244,fusion ? s->height-56 : s->height-68);
    int button_width=fusion ? w : 220;
    lv_obj_set_width(view_button,button_width); lv_obj_set_width(connections_button,button_width);
}

void carplay_fusion_ui_init(lv_obj_t *launcher, uint16_t width, uint16_t height)
{
    if (dashboard || carplay_fusion_init(width,height)) return;
    launcher_screen=launcher;
    lv_obj_t *back=make_button(launcher,"Resume CarPlay",show_dashboard);
    lv_obj_set_pos(back,410,36); lv_obj_set_width(back,220);
    dashboard=lv_obj_create(NULL);
    lv_obj_set_style_bg_color(dashboard,lv_color_hex(0x080F17),0);
    lv_obj_set_style_bg_opa(dashboard,LV_OPA_COVER,0);
    lv_obj_set_style_text_font(dashboard,&lv_font_montserrat_16,0);
    lv_obj_remove_flag(dashboard,LV_OBJ_FLAG_SCROLLABLE);
    background=lv_obj_create(dashboard);
    lv_obj_set_style_bg_color(background,lv_color_hex(0x122331),0);
    lv_obj_set_style_border_width(background,0,0); lv_obj_set_style_radius(background,12,0);
    lv_obj_remove_flag(background,LV_OBJ_FLAG_SCROLLABLE);
    title=make_label(dashboard,0xDDEBF4,false); text(title,"VEHICLE");
    source_label=make_label(dashboard,0xFFCA6B,false);
    speed=make_label(dashboard,0x72E8C3,true);
    gear=make_label(dashboard,0xEDF6FF,true); battery=make_label(dashboard,0xEDF6FF,true);
    connection=make_label(dashboard,0xB1C9DB,false); metrics=make_label(dashboard,0x8FA8B9,false);
    view_button=make_button(dashboard,"Display",open_display);
    connections_button=make_button(dashboard,"Home",show_connections);
    menu_button=make_button(dashboard,LV_SYMBOL_SETTINGS,open_display);
    lv_obj_set_pos(menu_button,width-56,8); lv_obj_set_size(menu_button,48,48);
    showing_launcher=true;
}

bool carplay_fusion_ui_update(const demo_status_t *status)
{
    if (!dashboard) return false;
    uint32_t now=rt_tick_get_millisecond();
    carplay_fusion_snapshot_t s; carplay_fusion_snapshot(now,&s);
    int result=carplay_launcher_fusion_apply(&s);
    if (result) {
        carplay_fusion_display_error(result);
        (void)carplay_fusion_set_mode(CARPLAY_LOCAL);
        carplay_fusion_snapshot(now,&s);
        (void)carplay_launcher_fusion_apply(&s);
    } else carplay_fusion_display_error(0);
    if (s.effective != CARPLAY_LOCAL) showing_launcher=false;
    if (showing_launcher) {
        if (lv_screen_active() != launcher_screen) lv_screen_load(launcher_screen);
        return false;
    }
    if (lv_screen_active() != dashboard) lv_screen_load(dashboard);
    arrange(&s);
    if (s.effective == CARPLAY_FULLSCREEN) return true;
    char line[256];
    text(source_label,s.vehicle.simulated ? "SIMULATED DATA" : s.vehicle.available ? "VEHICLE PROVIDER" : "DATA UNAVAILABLE");
    if (s.vehicle.available) {
        snprintf(line,sizeof(line),"%u km/h",s.vehicle.speed_kph); text(speed,line);
        snprintf(line,sizeof(line),TR("GEAR  %s"),s.vehicle.gear); text(gear,line);
        snprintf(line,sizeof(line),TR("SOC  %u%%"),s.vehicle.battery_percent); text(battery,line);
    } else { text(speed,"-- km/h"); text(gear,"GEAR  --"); text(battery,"SOC  --"); }
    const char *stage=status->stage == DEMO_LIVE ? "CONNECTED" : status->stage == DEMO_CLOSING ? "CLOSING" :
        status->stage == DEMO_FAILED ? "CONNECTION ERROR" : status->stage >= DEMO_PAIR ? "CONNECTING" : "WAITING FOR PHONE";
    snprintf(line,sizeof(line),"%s\n%s | %s",TR(status->display_reconfiguring ? "Switching..." : stage),
        TR(carplay_fusion_mode_name(s.effective)),TR(status->wireless ? "WIRELESS" : "USB")); text(connection,line);
    carplay_video_perf_t p; carplay_video_sink_perf(&p);
    uint32_t elapsed=now-stats_ms;
    if (!stats_ms || elapsed >= 1000U) {
        rate_tenths=stats_ms && p.latched >= stats_frames ? (unsigned)((uint64_t)(p.latched-stats_frames)*10000U/elapsed) : 0;
        stats_ms=now; stats_frames=p.latched;
    }
    char cpu[16]="n/a", heap[24]="n/a";
#if defined(LPKG_USING_CPU_USAGE)
    float load=cpu_load_average();
    if (load >= 0 && load <= 100) snprintf(cpu,sizeof(cpu),"%u%%",(unsigned)load);
#endif
#if defined(RT_USING_HEAP)
    rt_size_t total,used,peak; rt_memory_info(&total,&used,&peak);
    snprintf(heap,sizeof(heap),"%u KiB",(unsigned)(used/1024U));
#endif
    snprintf(line,sizeof(line),"%u.%u fps\nCPU %s | %s",
        rate_tenths/10,rate_tenths%10,cpu,
        heap); text(metrics,line);
    return true;
}
#endif
