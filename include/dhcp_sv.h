#ifndef DHCP_SV_H
#define DHCP_SV_H

#include <stdbool.h>
#include "config.h"

/* LAN DHCP server: writes a dnsmasq.conf from cfg, forks dnsmasq, tracks it. */

int  dhcp_sv_start(const rd_config *cfg);
int  dhcp_sv_reload(void);     /* SIGHUP to dnsmasq to re-read config */
bool dhcp_sv_running(void);    /* true if child_pid > 0 */
int  dhcp_sv_stop(void);       /* SIGTERM + reap; idempotent */

#endif
