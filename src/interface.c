#define _POSIX_C_SOURCE 200809L

#include "interface.h"
#include "config.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/wait.h>
#define SYS_NET_PATH "/sys/class/net"

/* Returns 1 if up, 0 if down/-1 on read error. */
static int read_operstate(const char *name) {
    char path[300];
    snprintf(path, sizeof(path), "%s/%s/operstate", SYS_NET_PATH, name);

    FILE *fp = fopen(path, "r");
    if (fp == NULL) {
        return -1;
    }
    char state[16];
    int n = fscanf(fp, "%15s", state);
    fclose(fp);
    return (n == 1 && strcmp(state, "up") == 0) ? 1 : 0;
}

int interface_init(void) {
    struct stat st;
    if (stat(SYS_NET_PATH, &st) != 0 || !S_ISDIR(st.st_mode)) {
        log_error("interface: %s missing", SYS_NET_PATH);
        return -1;
    }
    return 0;
}

int interface_refresh(void) {
    return 0;  /* operstate is read fresh each is_up() call */
}

bool interface_is_up(const char *ifname) {
    if (ifname == NULL || ifname[0] == '\0') {
        return false;
    }
    return read_operstate(ifname) == 1;
}

void interface_get_roles(char *wan, size_t w_size, char *lan, size_t l_size) {
    if (wan != NULL && w_size > 0) wan[0] = '\0';
    if (lan != NULL && l_size > 0) lan[0] = '\0';

    const rd_config *cfg = config_get();
    if (cfg == NULL) return;

    if (wan != NULL && w_size > 0) snprintf(wan, w_size, "%s", cfg->wan_iface);
    if (lan != NULL && l_size > 0) snprintf(lan, l_size, "%s", cfg->lan_iface);
}

/* Run `ip ...` and return its exit status (0 on success, -1 on fork failure). */
static int run_ip(const char *cmd) {
    int rc = system(cmd);
    if (rc == -1) {
        log_error("interface: fork failed: %s", strerror(errno));
        return -1;
    }
    if (!WIFEXITED(rc) || WEXITSTATUS(rc) != 0) {
        log_error("interface: %s failed (rc=%d)", cmd, WEXITSTATUS(rc));
        return -1;
    }
    return 0;
}

int iface_up(const char *name) {
    if (name == NULL || name[0] == '\0') return -1;
    char cmd[300];
    snprintf(cmd, sizeof(cmd), "ip link set %s up", name);
    log_info("interface: %s up", name);
    return run_ip(cmd);
}

int iface_add_addr(const char *name, const char *cidr) {
    if (name == NULL || name[0] == '\0' || cidr == NULL || cidr[0] == '\0') return -1;
    char cmd[300];
    snprintf(cmd, sizeof(cmd), "ip addr add %s dev %s", cidr, name);
    log_info("interface: %s addr add %s", name, cidr);
    return run_ip(cmd);
}

int iface_flush_addrs(const char *name) {
    if (name == NULL || name[0] == '\0') return -1;
    char cmd[300];
    snprintf(cmd, sizeof(cmd), "ip addr flush dev %s", name);
    log_info("interface: %s flush addrs", name);
    return run_ip(cmd);
}
