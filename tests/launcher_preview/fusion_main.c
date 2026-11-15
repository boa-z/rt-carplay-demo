/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Actual LVGL software rendering. A modeled video latch is layout input,
 * never an assertion about DE, iPhone negotiation or official Ultra. */
#include <assert.h>
#include <stdlib.h>
#include "../../ui/fusion_dashboard.c"

static uint8_t pixels[1024*600*4];
static uint32_t now=123000;
static unsigned irq_depth;
rt_base_t rt_hw_interrupt_disable(void) { return irq_depth++; }
void rt_hw_interrupt_enable(rt_base_t level) { assert(level+1 == irq_depth); --irq_depth; }
uint32_t rt_tick_get_millisecond(void) { return now; }
int rt_kprintf(const char *format, ...) { (void)format; return 0; }
int carplay_launcher_fusion_apply(const carplay_fusion_snapshot_t *s)
{ assert(s->initialized && !irq_depth); return RTCP_OK; }
void carplay_video_sink_perf(carplay_video_perf_t *p) { memset(p,0,sizeof(*p)); }
static void flush(lv_display_t *d, const lv_area_t *area, uint8_t *data)
{ (void)area; (void)data; lv_display_flush_ready(d); }

static void inside(lv_obj_t *object, int min_x)
{
    lv_area_t a; lv_obj_get_coords(object,&a);
    assert(a.x1 >= min_x && a.y1 >= 0 && a.x2 < 1024 && a.y2 < 600);
}
static void screenshot(const char *directory, const char *name, demo_status_t *status, int min_x)
{
    assert(carplay_fusion_ui_update(status));
    lv_refr_now(NULL);
    lv_obj_t *objects[]={title,source_label,speed,gear,battery,connection,metrics,
        view_button,connections_button,full_button};
    for (unsigned i=0;i<sizeof(objects)/sizeof(objects[0]);++i) inside(objects[i],min_x);
    char path[512]; snprintf(path,sizeof(path),"%s/%s.ppm",directory,name);
    FILE *f=fopen(path,"wb"); assert(f);
    fprintf(f,"P6\n1024 600\n255\n");
    for (unsigned i=0;i<1024*600;++i) {
        uint8_t rgb[]={pixels[i*4+2],pixels[i*4+1],pixels[i*4]};
        assert(fwrite(rgb,1,3,f) == 3);
    }
    assert(fclose(f) == 0);
}
int main(int argc, char **argv)
{
    assert(argc == 2); lv_init();
    lv_display_t *d=lv_display_create(1024,600); assert(d);
    lv_display_set_color_format(d,LV_COLOR_FORMAT_XRGB8888);
    lv_display_set_buffers(d,pixels,NULL,sizeof(pixels),LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(d,flush);
    lv_obj_t *launcher=lv_obj_create(NULL);
    demo_status_t status={.stage=DEMO_WAIT};
    carplay_fusion_ui_init(launcher,1024,600);
    screenshot(argv[1],"fusion-local-simulated",&status,0);
    assert(!strcmp(lv_label_get_text(source_label),"SIMULATED DATA"));
    assert(strstr(lv_label_get_text(metrics),"CPU n/a | heap n/a"));
    rtcp_display_rect_t rect={.x=0,.y=75,.width=768,.height=450};
    carplay_fusion_snapshot_t s; carplay_fusion_snapshot(now,&s);
    carplay_fusion_video_latched(&rect,1024,600,s.generation,now);
    status.stage=DEMO_LIVE;
    screenshot(argv[1],"fusion-connected-simulated",&status,768);
    carplay_vehicle_sample_t provider={.available=true,.speed_kph=42,.battery_percent=77,.gear="R"};
    assert(carplay_fusion_set_vehicle(&provider) == RTCP_OK);
    screenshot(argv[1],"fusion-provider",&status,768);
    assert(!strcmp(lv_label_get_text(speed),"42 km/h"));
    provider.available=false; assert(carplay_fusion_set_vehicle(&provider) == RTCP_OK);
    screenshot(argv[1],"fusion-provider-unavailable",&status,768);
    assert(!strcmp(lv_label_get_text(speed),"-- km/h"));
    lv_obj_send_event(connections_button,LV_EVENT_CLICKED,NULL);
    assert(!carplay_fusion_ui_update(&status) && lv_screen_active() == launcher);
    show_dashboard(NULL); assert(carplay_fusion_ui_update(&status));
    lv_obj_send_event(full_button,LV_EVENT_CLICKED,NULL);
    assert(carplay_fusion_ui_update(&status));
    carplay_fusion_snapshot(now,&s); assert(s.effective == CARPLAY_FULLSCREEN);
    carplay_fusion_video_stopped(); status.stage=DEMO_FAILED; now+=3000;
    carplay_fusion_use_simulated();
    screenshot(argv[1],"fusion-disconnected",&status,0);
    assert(strstr(lv_label_get_text(connection),"CONNECTION ERROR"));
    puts("Actual LVGL fusion layout, provider data, mode buttons and disconnect rendering passed (Host only)");
    return 0;
}
