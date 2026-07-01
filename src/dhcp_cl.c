#define _POSIX_C_SOURCE 200809L

#include "dhcp_cl.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

/* WAN DHCP client: wraps BusyBox udhcpc. Lease at /var/lib/udhcpc/<iface>.lease. */

#define UDHCP_LEASE_DIR "/var/lib/udhcpc"

static pid_t child_pid   = -1;
static char  lease_path[256];

static int ensure_lease_dir(void) {
    if (mkdir(UDHCP_LEASE_DIR, 0755) < 0 && errno != EEXIST) {
        log_error("dhcp_client: mkdir %s: %s", UDHCP_LEASE_DIR, strerror(errno));
        return -1;
    }
    return 0;
}

int dhcp_cl_start(const char *iface) {
    if (iface == NULL || iface[0] == '\0') {
        log_error("dhcp_client: empty interface name");
        return -1;
    }
    if (child_pid > 0) {
        log_warn("dhcp_client: already running (pid %d), stopping first", child_pid);
        dhcp_cl_stop();
    }
    if (ensure_lease_dir() < 0) return -1;
    snprintf(lease_path, sizeof(lease_path), "%s/%s.lease", UDHCP_LEASE_DIR, iface);

    log_info("dhcp_client: starting udhcpc on %s", iface);

    pid_t pid = fork();
    if (pid < 0) {
        log_error("dhcp_client: fork failed: %s", strerror(errno));
        return -1;
    }
    if (pid == 0) {
        /* -i iface, -p lease path, -f foreground, -q quiet, -t 1 try-once. */
        execlp("udhcpc", "udhcpc",
               "-i", iface, "-p", lease_path,
               "-f", "-q", "-t", "1", (char *)NULL);
        log_error("dhcp_client: execlp udhcpc failed: %s", strerror(errno));
        _exit(127);
    }
    child_pid = pid;
    log_info("dhcp_client: udhcpc started (pid %d, lease=%s)", pid, lease_path);
    return 0;
}

int dhcp_cl_stop(void) {
    if (child_pid <= 0) return 0;
    log_info("dhcp_client: stopping udhcpc (pid %d)", child_pid);
    if (kill(child_pid, SIGTERM) < 0 && errno != ESRCH) {
        log_warn("dhcp_client: SIGTERM failed (%s), trying SIGKILL", strerror(errno));
        kill(child_pid, SIGKILL);
    }
    int status;
    waitpid(child_pid, &status, 0);
    child_pid = -1;
    return 0;
}

int dhcp_cl_renew(void) {
    if (child_pid <= 0) return -1;
    log_info("dhcp_client: sending SIGUSR1 to udhcpc (pid %d)", child_pid);
    if (kill(child_pid, SIGUSR1) < 0) {
        log_error("dhcp_client: SIGUSR1 failed: %s", strerror(errno));
        return -1;
    }
    return 0;
}

bool dhcp_cl_lease(void) {
    if (lease_path[0] == '\0') return false;
    FILE *f = fopen(lease_path, "r");
    if (!f) return false;
    char line[256];
    bool found = false;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "ip=", 3) == 0) { found = true; break; }
    }
    fclose(f);
    return found;
}
