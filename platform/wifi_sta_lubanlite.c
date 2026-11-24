#include <rtthread.h>
#include <rtdevice.h>
#include <wlan_mgnt.h>
#include <stdio.h>
#include <string.h>
#include "connection_manager.h"
#include "network_manager.h"
#include "wifi_sta_lubanlite.h"

enum wifi_operation
{
    WIFI_IDLE,
    WIFI_ENABLE,
    WIFI_DISABLE,
    WIFI_SCAN,
    WIFI_JOIN,
    WIFI_FORGET
};

static struct rt_mutex wifi_lock;
static struct rt_semaphore wifi_wake;
static bool initialized;
static enum wifi_operation request;
static carplay_wifi_status_t status;
static struct rt_wlan_info scanned[CARPLAY_WIFI_SCAN_MAX], selected, reconnect;
static char pending_password[65], reconnect_password[65];
static bool have_network;
static unsigned retry_delay;

static void wipe(char *secret, size_t length)
{
    volatile char *next = secret;
    while (length--)
        *next++ = 0;
}

static const char *security_name(rt_wlan_security_t security)
{
    switch (security)
    {
    case SECURITY_OPEN:
        return "Open";
    case SECURITY_WPA_TKIP_PSK:
    case SECURITY_WPA_AES_PSK:
        return "WPA";
    case SECURITY_WPA2_AES_PSK:
    case SECURITY_WPA2_TKIP_PSK:
    case SECURITY_WPA2_MIXED_PSK:
        return "WPA2";
    default:
        return "Unsupported";
    }
}

static void scan_report(int event, struct rt_wlan_buff *buff, void *parameter)
{
    (void)event;
    (void)parameter;
    if (!buff || !buff->data || buff->len != sizeof(struct rt_wlan_info))
        return;
    const struct rt_wlan_info *result = buff->data;
    if (!result->ssid.len || result->ssid.len > 32)
        return;
    rt_mutex_take(&wifi_lock, RT_WAITING_FOREVER);
    unsigned index = 0;
    while (index < status.count &&
           memcmp(scanned[index].bssid, result->bssid, sizeof(result->bssid)))
        ++index;
    if (index < CARPLAY_WIFI_SCAN_MAX)
    {
        if (index == status.count)
            ++status.count;
        scanned[index] = *result;
        carplay_wifi_access_point_t *out = &status.results[index];
        for (unsigned offset = 0; offset < result->ssid.len; ++offset)
        {
            unsigned char character = result->ssid.val[offset];
            out->ssid[offset] = character >= 32 && character < 127 ? (char)character : '.';
        }
        out->ssid[result->ssid.len] = 0;
        snprintf(out->security, sizeof(out->security), "%s", security_name(result->security));
        out->rssi = result->rssi;
        out->channel = result->channel;
    }
    rt_mutex_release(&wifi_lock);
}

static int enter_sta(void)
{
    if (!carplay_connection_maintenance_acquire())
        return -RT_EBUSY;
    rt_wlan_config_autoreconnect(RT_FALSE);
    int result = rt_wlan_set_mode(RT_WLAN_DEVICE_AP_NAME, RT_WLAN_NONE);
    if (result == RT_EOK)
        result = rt_wlan_set_mode(RT_WLAN_DEVICE_STA_NAME, RT_WLAN_STATION);
    if (result != RT_EOK)
    {
        (void)rt_wlan_set_mode(RT_WLAN_DEVICE_STA_NAME, RT_WLAN_NONE);
        carplay_connection_maintenance_release();
    }
    return result;
}

static int leave_sta(void)
{
    if (rt_wlan_is_connected())
        (void)rt_wlan_disconnect();
    int result = rt_wlan_set_mode(RT_WLAN_DEVICE_STA_NAME, RT_WLAN_NONE);
    if (result == RT_EOK)
        carplay_connection_maintenance_release();
    return result;
}

static void poll_state(void)
{
    bool connected = status.enabled && rt_wlan_is_connected();
    bool addressed = connected && rt_wlan_is_ready();
    rt_mutex_take(&wifi_lock, RT_WAITING_FOREVER);
    status.connected = connected;
    status.addressed = addressed;
    snprintf(status.connected_ssid, sizeof(status.connected_ssid), "%.*s",
             connected ? reconnect.ssid.len : 0, reconnect.ssid.val);
    rt_mutex_release(&wifi_lock);
}

static void wifi_worker(void *argument)
{
    (void)argument;
    for (;;)
    {
        (void)rt_sem_take(&wifi_wake, rt_tick_from_millisecond(1000));
        rt_mutex_take(&wifi_lock, RT_WAITING_FOREVER);
        enum wifi_operation operation = request;
        request = WIFI_IDLE;
        rt_mutex_release(&wifi_lock);
        int result = RT_EOK;
        const char *message = NULL;
        bool network_change = operation != WIFI_IDLE && carplay_network_change_begin();
        if (operation != WIFI_IDLE && !network_change)
        {
            rt_mutex_take(&wifi_lock, RT_WAITING_FOREVER);
            wipe(pending_password, sizeof(pending_password));
            snprintf(status.message, sizeof(status.message), "Network is in use by the update worker.");
            status.busy = false;
            rt_mutex_release(&wifi_lock);
            continue;
        }
        switch (operation)
        {
        case WIFI_ENABLE:
            result = enter_sta();
            message = result ? "Could not enter STA maintenance." : "STA maintenance active. Scan for networks.";
            break;
        case WIFI_DISABLE:
            result = leave_sta();
            message = result ? "Could not stop STA. Retry before starting CarPlay." : "CarPlay connection policy restored.";
            break;
        case WIFI_SCAN:
            result = rt_wlan_register_event_handler(RT_WLAN_EVT_SCAN_REPORT, scan_report, NULL);
            if (!result)
            {
                result = rt_wlan_scan_with_info(NULL);
                rt_wlan_unregister_event_handler(RT_WLAN_EVT_SCAN_REPORT);
            }
            message = result ? "Scan failed or timed out." : "Select a Wi-Fi network.";
            break;
        case WIFI_JOIN:
            result = rt_wlan_connect_adv(&selected, pending_password);
            if (!result)
            {
                reconnect = selected;
                strcpy(reconnect_password, pending_password);
                have_network = true;
            }
            wipe(pending_password, sizeof(pending_password));
            retry_delay = 10;
            message = result ? "Wi-Fi association failed." : "Associated. Waiting for DHCP.";
            break;
        case WIFI_FORGET:
            have_network = false;
            wipe(reconnect_password, sizeof(reconnect_password));
            if (rt_wlan_is_connected())
                result = rt_wlan_disconnect();
            message = result ? "Disconnect failed." : "Wi-Fi network forgotten.";
            break;
        case WIFI_IDLE:
            if (status.enabled && have_network && !rt_wlan_is_connected())
            {
                if (retry_delay)
                    --retry_delay;
                else if (carplay_network_change_begin())
                {
                    result = rt_wlan_connect_adv(&reconnect, reconnect_password);
                    carplay_network_change_end();
                    retry_delay = 10;
                    message = result ? "Wi-Fi reconnect failed." : "Reconnected. Waiting for DHCP.";
                }
            }
            break;
        }
        if (network_change)
            carplay_network_change_end();
        rt_mutex_take(&wifi_lock, RT_WAITING_FOREVER);
        if (!result && (operation == WIFI_ENABLE || operation == WIFI_DISABLE))
            status.enabled = operation == WIFI_ENABLE;
        if (message)
            snprintf(status.message, sizeof(status.message), "%s", message);
        if (operation != WIFI_IDLE)
            status.busy = false;
        rt_mutex_release(&wifi_lock);
        poll_state();
    }
}

static int wifi_service_init(void)
{
    rt_mutex_init(&wifi_lock, "cpwifi", RT_IPC_FLAG_PRIO);
    rt_sem_init(&wifi_wake, "cpwifi", 0, RT_IPC_FLAG_FIFO);
    snprintf(status.message, sizeof(status.message), "STA maintenance pauses CarPlay until you leave it.");
    rt_thread_t thread = rt_thread_create("cpwifi", wifi_worker, NULL, 6144, 23, 10);
    if (!thread)
        return -RT_ENOMEM;
    int result = rt_thread_startup(thread);
    if (result)
        rt_thread_delete(thread);
    initialized = result == RT_EOK;
    return result;
}
INIT_APP_EXPORT(wifi_service_init);

void carplay_wifi_status(carplay_wifi_status_t *out)
{
    if (!initialized)
    {
        memset(out, 0, sizeof(*out));
        return;
    }
    rt_mutex_take(&wifi_lock, RT_WAITING_FOREVER);
    *out = status;
    rt_mutex_release(&wifi_lock);
}

static int queue(enum wifi_operation operation, unsigned revision, unsigned index, const char *password)
{
    if (!initialized)
        return -RT_ERROR;
    rt_mutex_take(&wifi_lock, RT_WAITING_FOREVER);
    int result = -RT_EINVAL;
    if (status.busy)
        result = -RT_EBUSY;
    else if (operation == WIFI_ENABLE ? !status.enabled : status.enabled)
    {
        result = RT_EOK;
        if (operation == WIFI_JOIN)
        {
            size_t length = strlen(password);
            if (revision != status.scan_revision || index >= status.count ||
                !strcmp(status.results[index].security, "Unsupported") ||
                (!strcmp(status.results[index].security, "Open") ? length != 0 : length < 8 || length > 63))
                result = -RT_EINVAL;
            else
            {
                selected = scanned[index];
                strcpy(pending_password, password);
            }
        }
        if (!result)
        {
            if (operation == WIFI_SCAN)
            {
                ++status.scan_revision;
                status.count = 0;
            }
            request = operation;
            status.busy = true;
            rt_sem_release(&wifi_wake);
        }
    }
    rt_mutex_release(&wifi_lock);
    return result;
}

int carplay_wifi_enable(bool enabled)
{
    return queue(enabled ? WIFI_ENABLE : WIFI_DISABLE, 0, 0, NULL);
}

int carplay_wifi_scan(void)
{
    return queue(WIFI_SCAN, 0, 0, NULL);
}

int carplay_wifi_join(unsigned revision, unsigned index, const char *password)
{
    return password ? queue(WIFI_JOIN, revision, index, password) : -RT_EINVAL;
}

int carplay_wifi_forget(void)
{
    return queue(WIFI_FORGET, 0, 0, NULL);
}
