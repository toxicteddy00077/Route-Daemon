#ifndef DHCP_CL_H
#define DHCP_CL_H

#include <stdbool.h>

/* Start the DHCP client on the given interface (e.g. "wlan0").
 * Forks udhcpc, stores the child PID, returns 0/-1. */
int  dhcp_cl_start(const char *iface);

/* Ask udhcpc to renew its lease (sends SIGUSR1). */
int  dhcp_cl_renew(void);

/* True if a valid lease file exists for the running client. */
bool dhcp_cl_lease(void);

/* Stop the DHCP client: SIGTERM the child, reap it. Idempotent. */
int  dhcp_cl_stop(void);

#endif
