#ifndef HEALTH_H
#define HEALTH_H

#include <stdbool.h>

// Snapshot of subsystem state. Populated by health_snapshot().
struct rd_health {
    int  uptime_sec;
    bool wan_up;
    bool lan_up;
    bool dhcp_client_running;
    bool dhcp_server_running;
    bool hostapd_running;
    bool firewall_applied;
    bool shaper_applied;
};

// Init start-time counter (call once at boot).
void health_init(void);

// Populate h with current subsystem state. Returns 0.
int  health_snapshot(struct rd_health *h);

// True if every subsystem reports healthy.
bool health_is_ok(const struct rd_health *h);

#endif
