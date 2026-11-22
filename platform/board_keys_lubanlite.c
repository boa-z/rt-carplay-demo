#include <limits.h>
#include <stdatomic.h>

#include <rtthread.h>
#include <rtdevice.h>

#include "carplay_app.h"

#define KEYADC_CHANNEL      6
#define KEYADC_SAMPLE_MS    20
#define KEYADC_STABLE_READS 3

typedef enum
{
    BOARD_KEY_UNKNOWN,
    BOARD_KEY_RELEASED,
    BOARD_KEY_UP,
    BOARD_KEY_DOWN,
    BOARD_KEY_LEFT,
    BOARD_KEY_RIGHT,
} board_key_t;

typedef struct
{
    board_key_t candidate;
    unsigned stable_reads;
    bool armed;
} key_debounce_t;

static atomic_uint latest_adc = ATOMIC_VAR_INIT(UINT_MAX);

static board_key_t board_key_decode(unsigned sample)
{
    if (sample >= 3600U && sample <= 4095U)
        return BOARD_KEY_RELEASED;

    static const struct
    {
        unsigned low;
        unsigned high;
        board_key_t key;
    } windows[] = {
        { 230U, 590U, BOARD_KEY_UP },
        { 900U, 1280U, BOARD_KEY_DOWN },
        { 1770U, 2200U, BOARD_KEY_LEFT },
        { 2460U, 2910U, BOARD_KEY_RIGHT },
    };
    for (unsigned index = 0; index < sizeof(windows) / sizeof(windows[0]); ++index)
        if (sample >= windows[index].low && sample <= windows[index].high)
            return windows[index].key;
    return BOARD_KEY_UNKNOWN;
}

static board_key_t board_key_update(key_debounce_t *state, unsigned sample)
{
    board_key_t key = board_key_decode(sample);
    if (state->candidate != key)
    {
        state->candidate = key;
        state->stable_reads = 0;
    }
    if (state->stable_reads >= KEYADC_STABLE_READS || ++state->stable_reads < KEYADC_STABLE_READS)
        return BOARD_KEY_UNKNOWN;
    if (key == BOARD_KEY_RELEASED)
        state->armed = true;
    if (key <= BOARD_KEY_RELEASED || !state->armed)
        return BOARD_KEY_UNKNOWN;
    state->armed = false;
    return key;
}

static void board_key_dispatch(board_key_t key)
{
    switch (key)
    {
    case BOARD_KEY_UP:
        (void)carplay_app_phone_action(CARPLAY_PHONE_BACK);
        break;
    case BOARD_KEY_DOWN:
        (void)carplay_app_phone_action(CARPLAY_PHONE_HOME);
        break;
    case BOARD_KEY_LEFT:
        (void)carplay_app_request_siri();
        break;
    case BOARD_KEY_RIGHT:
        (void)carplay_app_phone_action(CARPLAY_PHONE_PLAY_PAUSE);
        break;
    default:
        break;
    }
}

static void board_key_worker(void *context)
{
    rt_device_t device = context;
    key_debounce_t state = { 0 };
    for (;;)
    {
        rt_uint32_t sample;
        if (rt_device_read(device, KEYADC_CHANNEL, &sample, sizeof(sample)) != sizeof(sample))
            sample = UINT_MAX;
        atomic_store_explicit(&latest_adc, sample, memory_order_relaxed);
        board_key_dispatch(board_key_update(&state, sample));
        rt_thread_mdelay(KEYADC_SAMPLE_MS);
    }
}

static int board_key_init(void)
{
    rt_device_t device = rt_device_find("gpai");
    if (!device)
        return -RT_ENOSYS;
    rt_err_t result = rt_device_open(device, RT_DEVICE_OFLAG_RDONLY);
    if (result != RT_EOK)
        return result;
    result = rt_adc_enable((rt_adc_device_t)device, KEYADC_CHANNEL);
    if (result == RT_EOK)
    {
        rt_thread_t worker = rt_thread_create("cpkeys", board_key_worker, device, 2048, 24, 10);
        if (!worker)
            result = -RT_ENOMEM;
        else
        {
            result = rt_thread_startup(worker);
            if (result == RT_EOK)
                return RT_EOK;
            (void)rt_thread_delete(worker);
        }
        (void)rt_adc_disable((rt_adc_device_t)device, KEYADC_CHANNEL);
    }
    (void)rt_device_close(device);
    return result;
}
INIT_APP_EXPORT(board_key_init);

#ifdef RT_USING_FINSH
static void board_key_status(void)
{
    static const char * const names[] = { "unknown", "released", "K4 UP", "K5 DOWN", "K6 LEFT", "K7 RIGHT" };
    unsigned sample = atomic_load_explicit(&latest_adc, memory_order_relaxed);
    if (sample == UINT_MAX)
        rt_kprintf("KEYADC PA.6: no sample\n");
    else
        rt_kprintf("KEYADC PA.6: %u (%s)\n", sample, names[board_key_decode(sample)]);
}
MSH_CMD_EXPORT_ALIAS(board_key_status, carplay_board_keys, Show the latest D213 function key sample);
#endif
