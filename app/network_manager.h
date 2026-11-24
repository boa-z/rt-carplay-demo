#ifndef CARPLAY_NETWORK_MANAGER_H
#define CARPLAY_NETWORK_MANAGER_H

#include <stdbool.h>

typedef enum
{
    CARPLAY_WAN_NONE,
    CARPLAY_WAN_ETHERNET,
    CARPLAY_WAN_STA
} carplay_wan_t;

bool carplay_network_acquire(bool allow_local);
bool carplay_network_valid(void);
const char *carplay_network_lease_name(void);
bool carplay_network_change_begin(void);
void carplay_network_change_end(void);
void carplay_network_release(void);
carplay_wan_t carplay_network_wan(void);

#endif
