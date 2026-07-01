#ifndef INTERFACE_H
#define INTERFACE_H

#include <stdbool.h>
#include <stddef.h>

/* Initialize the interface module. Verifies /sys/class/net exists and
 * performs the first refresh. Returns 0 on success, -1 if the sysfs
 * path is missing (container without host networking, etc.). */
int  interface_init(void);

/* Re-scan /sys/class/net, update the cached state, log transitions.
 * Call once per main-loop tick. Returns 0 on success. */
int  interface_refresh(void);

/* Is the named interface currently up? Returns false if the interface
 * does not exist or its operstate file cannot be read. */
bool interface_is_up(const char *ifname);

/* Copy the configured wan/lan interface names from config_get() into
 * the supplied buffers (NUL-terminated, truncated if too small).
 * Either buffer may be NULL if that role isn't needed. */
void interface_get_roles(char *wan, size_t w_size,
                         char *lan, size_t l_size);

/* Action helpers — Phase 1 setup. Thin wrappers around the `ip` command.
 * All return 0 on success, -1 on failure. */

/* Bring the named interface up (`ip link set <name> up`). No-op if
 * already up. Returns -1 if the interface doesn't exist or you lack
 * CAP_NET_ADMIN. */
int iface_up(const char *name);

/* Bring the named interface down (`ip link set <name> down`). */
int iface_down(const char *name);

/* Add an IP address in CIDR form (`ip addr add <cidr> dev <name>`).
 * Example cidr: "192.168.50.1/24". */
int iface_add_addr(const char *name, const char *cidr);

/* Remove all IP addresses from the interface (`ip addr flush dev <name>`).
 * Used to give the WAN a clean slate before udhcpc claims it in Phase 1. */
int iface_flush_addrs(const char *name);

#endif
