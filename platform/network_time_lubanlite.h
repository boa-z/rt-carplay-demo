#ifndef CARPLAY_NETWORK_TIME_LUBANLITE_H
#define CARPLAY_NETWORK_TIME_LUBANLITE_H

#define CARPLAY_TIME_SERVER_MAX 254

typedef enum
{
    CARPLAY_TIME_IDLE,
    CARPLAY_TIME_SYNCING,
    CARPLAY_TIME_SYNCED,
    CARPLAY_TIME_NO_WAN,
    CARPLAY_TIME_NO_SERVER,
    CARPLAY_TIME_NO_REPLY,
    CARPLAY_TIME_RTC_ERROR,
    CARPLAY_TIME_UNAVAILABLE
} carplay_time_status_t;

void carplay_network_time_start(void);
carplay_time_status_t carplay_network_time_status(void);
void carplay_network_time_server_get(char server[CARPLAY_TIME_SERVER_MAX]);
int carplay_network_time_server_set(const char *server);
int carplay_network_time_server_reset(void);

#endif
