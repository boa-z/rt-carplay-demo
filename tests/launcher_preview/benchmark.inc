/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <time.h>

enum
{
    BENCH_FRAMES = 240
};

static double timestamp_ms(void)
{
    struct timespec stamp;
    timespec_get(&stamp, TIME_UTC);
    return stamp.tv_sec * 1000.0 + stamp.tv_nsec / 1000000.0;
}

static unsigned object_count(lv_obj_t *parent)
{
    unsigned count = 1;
    for (uint32_t index = 0; index < lv_obj_get_child_count(parent); ++index)
        count += object_count(lv_obj_get_child(parent, (int32_t)index));
    return count;
}

static int compare_sample(const void *left, const void *right)
{
    double first = *(const double *)left;
    double second = *(const double *)right;
    return (first > second) - (first < second);
}

static void print_samples(const char *name, double *series)
{
    qsort(series, BENCH_FRAMES, sizeof(*series), compare_sample);
    printf("%s median_ms=%.3f p95_ms=%.3f max_ms=%.3f\n", name,
           series[BENCH_FRAMES / 2], series[BENCH_FRAMES * 95 / 100 - 1],
           series[BENCH_FRAMES - 1]);
}

static void run_benchmark(void)
{
    double start = timestamp_ms();
    carplay_demo_launcher_init();
    update_page(NULL);
    lv_refr_now(NULL);
    printf("init_first_frame_ms=%.3f objects=%u\n", timestamp_ms() - start,
           object_count(lv_screen_active()));

    double frame[BENCH_FRAMES], page[BENCH_FRAMES], theme[BENCH_FRAMES];
    for (unsigned index = 0; index < BENCH_FRAMES; ++index)
    {
        start = timestamp_ms();
        preview_ms += 250U;
        update_page(NULL);
        lv_refr_now(NULL);
        frame[index] = timestamp_ms() - start;
    }
    for (unsigned index = 0; index < BENCH_FRAMES; ++index)
    {
        start = timestamp_ms();
        show_page(index % 2 ? PAGE_DASHBOARD : PAGE_SETTINGS);
        lv_refr_now(NULL);
        page[index] = timestamp_ms() - start;
    }
    show_page(PAGE_SETTINGS);
    for (unsigned index = 0; index < BENCH_FRAMES; ++index)
    {
        start = timestamp_ms();
        carplay_ui_set_dark(index % 2 != 0);
        lv_refr_now(NULL);
        theme[index] = timestamp_ms() - start;
    }
    print_samples("dashboard_refresh", frame);
    print_samples("page_switch", page);
    print_samples("theme_switch", theme);
#if LV_USE_TJPGD
    show_page(PAGE_CONNECTIVITY);
    start = timestamp_ms();
    update_phone_cover(3);
    printf("jpeg_read_decode_ms=%.3f\n", timestamp_ms() - start);
    assert(!lv_obj_has_flag(phone_hub.cover, LV_OBJ_FLAG_HIDDEN));
#endif
}
