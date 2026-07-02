#ifndef WIFI_AP_H
#define WIFI_AP_H

#include <stdbool.h>
#include "config.h"

/* WiFi AP: switches the LAN iface to AP mode, writes hostapd.conf,
 * forks hostapd in foreground, tracks it. */

int  wifi_ap_start(const rd_config *cfg);
int  wifi_ap_stop(void);          /* kill hostapd, restore managed mode */
bool wifi_ap_running(void);
bool wifi_ap_active(void);        /* true if child started successfully */

#endif
