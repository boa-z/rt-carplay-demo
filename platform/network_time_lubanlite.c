#include <rtthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <lwip/apps/sntp.h>
#include <lwip/inet.h>
#include <lwip/netdb.h>
#include <lwip/tcpip.h>
#include "rtcp/rtcp.h"
#include "carplay_app.h"
#include "network_manager.h"
#include "network_time_lubanlite.h"

static atomic_int status;
static struct rt_semaphore sample_ready;
static uint32_t sample;
static struct rt_mutex server_lock;
static bool server_ready;
static char server_name[CARPLAY_TIME_SERVER_MAX] = RTCARPLAY_NTP_SERVER;
static char request_server[CARPLAY_TIME_SERVER_MAX];

#ifndef CARPLAY_TIME_SERVER_FILE
#define CARPLAY_TIME_SERVER_FILE "/data/.carplay-ntp-server"
#endif

static bool valid_server(const char *name)
{
    if (!name || !*name || strlen(name) >= CARPLAY_TIME_SERVER_MAX)
        return false;
    if (name[strspn(name, "0123456789.")] == '\0')
    {
        struct in_addr address;
        return inet_pton(AF_INET, name, &address) == 1;
    }
    unsigned label = 0;
    bool last_hyphen = false;
    for (const char *next = name; *next; ++next)
    {
        if (*next == '.')
        {
            if (!label || last_hyphen)
                return false;
            label = 0;
            continue;
        }
        last_hyphen = *next == '-';
        if (!(last_hyphen || (*next >= 'a' && *next <= 'z') ||
              (*next >= 'A' && *next <= 'Z') || (*next >= '0' && *next <= '9')) ||
            (!label && last_hyphen) || ++label > 63)
            return false;
    }
    return label && !last_hyphen;
}

static int time_service_init(void)
{
    if (rt_mutex_init(&server_lock, "cpntpcfg", RT_IPC_FLAG_PRIO) != RT_EOK)
        return -RT_ENOMEM;
    FILE *file = fopen(CARPLAY_TIME_SERVER_FILE, "rb");
    if (!file)
        file = fopen(CARPLAY_TIME_SERVER_FILE ".bak", "rb");
    if (file)
    {
        char saved[CARPLAY_TIME_SERVER_MAX];
        size_t length = fread(saved, 1, sizeof(saved) - 1, file);
        saved[length] = 0;
        if (fgetc(file) == EOF && !ferror(file) && strlen(saved) == length && valid_server(saved))
            strcpy(server_name, saved);
        fclose(file);
    }
    server_ready = true;
    return RT_EOK;
}
INIT_APP_EXPORT(time_service_init);

void carplay_network_time_server_get(char server[CARPLAY_TIME_SERVER_MAX])
{
    if (!server_ready)
    {
        strcpy(server, RTCARPLAY_NTP_SERVER);
        return;
    }
    rt_mutex_take(&server_lock, RT_WAITING_FOREVER);
    strcpy(server, server_name);
    rt_mutex_release(&server_lock);
}

int carplay_network_time_server_set(const char *server)
{
    if (!valid_server(server))
        return -RT_EINVAL;
    if (!server_ready)
        return -RT_ERROR;
    const char *temporary = CARPLAY_TIME_SERVER_FILE ".tmp";
    const char *backup = CARPLAY_TIME_SERVER_FILE ".bak";
    rt_mutex_take(&server_lock, RT_WAITING_FOREVER);
    if (!strcmp(server_name, server))
    {
        rt_mutex_release(&server_lock);
        return RT_EOK;
    }
    FILE *file = fopen(temporary, "wb");
    bool saved = false;
    if (file)
    {
        saved = fputs(server, file) >= 0 && !fflush(file) && !fsync(fileno(file));
        if (fclose(file))
            saved = false;
    }
    if (saved && rename(temporary, CARPLAY_TIME_SERVER_FILE))
    {
        FILE *previous = fopen(CARPLAY_TIME_SERVER_FILE, "rb");
        if (previous)
        {
            fclose(previous);
            remove(backup);
            saved = rename(CARPLAY_TIME_SERVER_FILE, backup) == 0;
        }
        if (saved && rename(temporary, CARPLAY_TIME_SERVER_FILE))
        {
            rename(backup, CARPLAY_TIME_SERVER_FILE);
            saved = false;
        }
    }
    if (saved)
    {
        strcpy(server_name, server);
        remove(backup);
    }
    else
        remove(temporary);
    rt_mutex_release(&server_lock);
    return saved ? RT_EOK : -RT_ERROR;
}

int carplay_network_time_server_reset(void)
{
    return carplay_network_time_server_set(RTCARPLAY_NTP_SERVER);
}

void carplay_network_time_sample(unsigned int seconds)
{
    if (seconds >= 1704067200U && !sample)
    {
        sample = seconds;
        rt_sem_release(&sample_ready);
    }
}

static carplay_time_status_t sync_clock(const char *name)
{
    const struct addrinfo hints = { .ai_family = AF_INET, .ai_socktype = SOCK_DGRAM };
    struct addrinfo *address = NULL;
    if (lwip_getaddrinfo(name, NULL, &hints, &address) != 0)
        return CARPLAY_TIME_NO_SERVER;
    ip_addr_t server;
    ip_addr_set_ip4_u32(&server, ((const struct sockaddr_in *)address->ai_addr)->sin_addr.s_addr);
    lwip_freeaddrinfo(address);
    if (!carplay_network_valid())
        return CARPLAY_TIME_NO_WAN;
    if (rt_sem_init(&sample_ready, "cpntp", 0, RT_IPC_FLAG_FIFO) != RT_EOK)
        return CARPLAY_TIME_UNAVAILABLE;
    LOCK_TCPIP_CORE();
    sample = 0;
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setserver(0, &server);
    sntp_init();
    bool started = sntp_enabled();
    UNLOCK_TCPIP_CORE();

    bool received = started &&
                    rt_sem_take(&sample_ready, rt_tick_from_millisecond(8000)) == RT_EOK;
    LOCK_TCPIP_CORE();
    sntp_stop();
    uint32_t seconds = sample;
    UNLOCK_TCPIP_CORE();
    rt_sem_detach(&sample_ready);

    if (!carplay_network_valid())
        return CARPLAY_TIME_NO_WAN;
    if (!started)
        return CARPLAY_TIME_UNAVAILABLE;
    if (!received)
        return CARPLAY_TIME_NO_REPLY;
    return carplay_app_set_network_clock(seconds) == RTCP_OK ? CARPLAY_TIME_SYNCED
                                                             : CARPLAY_TIME_RTC_ERROR;
}

static void time_worker(void *context)
{
    const char *name = context;
    struct in_addr local_address;
    carplay_time_status_t result = CARPLAY_TIME_NO_WAN;
    if (carplay_network_acquire(inet_pton(AF_INET, name, &local_address) == 1))
    {
        result = sync_clock(name);
        carplay_network_release();
    }
    atomic_store(&status, result);
}

void carplay_network_time_start(void)
{
    int previous = atomic_load(&status);
    do
    {
        if (previous == CARPLAY_TIME_SYNCING)
            return;
    } while (!atomic_compare_exchange_weak(&status, &previous, CARPLAY_TIME_SYNCING));

    carplay_network_time_server_get(request_server);
    rt_thread_t worker = rt_thread_create("cpntp", time_worker, request_server, 4096, 23, 10);
    if (worker && rt_thread_startup(worker) == RT_EOK)
        return;
    if (worker)
        rt_thread_delete(worker);
    atomic_store(&status, CARPLAY_TIME_UNAVAILABLE);
}

carplay_time_status_t carplay_network_time_status(void)
{
    return (carplay_time_status_t)atomic_load(&status);
}
