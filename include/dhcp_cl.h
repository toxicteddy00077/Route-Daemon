#ifndef DHCP_CL_H
#define DHCP_CL_H

#include <stdbool.h>

/* WAN DHCP client: forks udhcpc, tracks it, signals it, reaps it. */

int  dhcp_cl_start(const char *iface);
int  dhcp_cl_renew(void);     /* SIGUSR1 to udhcpc to renew */
bool dhcp_cl_lease(void);     /* true if lease_path has a valid "ip=" line */
int  dhcp_cl_stop(void);      /* SIGTERM + reap; idempotent */

#endif
