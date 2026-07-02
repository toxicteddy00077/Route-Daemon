#include "health.h"
#include "log.h"
#include "config.h"
#include "interface.h"
#include "dhcp_cl.h"
#include "dhcp_sv.h"
#include "wifi_ap.h"
#include "firewall.h"
#include "shaper.h"

#include <time.h>
#include <string.h>

static time_t start_time;

void health_init(void) { start_time = time(NULL); }

int health_snapshot(struct rd_health *h) {
    if (!h) return -1;
    memset(h, 0, sizeof(*h));
    h->uptime_sec = (int)(time(NULL) - start_time);

    char wan[32], lan[32];
    interface_get_roles(wan, sizeof(wan), lan, sizeof(lan));
    h->wan_up              = interface_is_up(wan);
    h->lan_up              = interface_is_up(lan);
    h->dhcp_client_running = dhcp_cl_running();
    h->dhcp_server_running = dhcp_sv_running();
    h->hostapd_running     = wifi_ap_running();
    h->firewall_applied    = firewall_is_active();
    h->shaper_applied      = shaper_is_active();
    return 0;
}

bool health_is_ok(const struct rd_health *h) {
    if (!h) return false;
    return h->wan_up && h->lan_up &&
           h->dhcp_client_running && h->dhcp_server_running &&
           h->hostapd_running && h->firewall_applied && h->shaper_applied;
}
