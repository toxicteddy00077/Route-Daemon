#ifndef INTERFACE_H
#define INTERFACE_H

#include <stdbool.h>
#include <stddef.h>

int  interface_init(void);

int  interface_refresh(void);

bool interface_is_up(const char *ifname);

void interface_get_roles(char *wan, size_t w_size,
                         char *lan, size_t l_size);

int iface_up(const char *name);

int iface_down(const char *name);

int iface_add_addr(const char *name, const char *cidr);

int iface_flush_addrs(const char *name);

#endif
