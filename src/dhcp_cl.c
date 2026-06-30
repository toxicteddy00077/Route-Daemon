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

/*
 * WAN DHCP client (Phase 1).
 *
 * Runs BusyBox's `udhcpc` as a child process and tracks it. The module's
 * job is small: fork+exec, remember the PID, signal it on demand, reap
 * it on shutdown. All DHCP protocol work happens inside udhcpc.
 *
 * Why a subprocess, not a library?
 *   - udhcpc is already in the image (busybox), zero install
 *   - it does exactly one thing, and is testable in isolation
 *   - no link deps
 *   - a library would mean re-implementing the DHCP state machine in C
 *
 * Lease file location: BusyBox default is /var/lib/udhcpc/<iface>.lease.
 * We make sure the directory exists before launching the child.
 */

#define UDHCP_LEASE_DIR "/var/lib/udhcpc"

static pid_t child_pid   = -1;
static char  lease_path[256];

/* mkdir UDHCP_LEASE_DIR; tolerate EEXIST (it's fine if already there). */
static int ensure_lease_dir(void) {
    if (mkdir(UDHCP_LEASE_DIR, 0755) < 0 && errno != EEXIST) {
        log_error("dhcp_client: mkdir %s: %s", UDHCP_LEASE_DIR, strerror(errno));
        return -1;
    }
    return 0;
}

int dhcp_cl_start(const char *iface) {
    /* 1. Validate input. */
    if (iface == NULL || iface[0] == '\0') {
        log_error("dhcp_client: empty interface name");
        return -1;
    }

    /* 2. If a previous client is still running, stop it first.
     *    This makes the function safe to call repeatedly. */
    if (child_pid > 0) {
        log_warn("dhcp_client: already running (pid %d), stopping first", child_pid);
        dhcp_cl_stop();
    }

    /* 3. udhcpc needs to write its lease file; the directory must exist. */
    if (ensure_lease_dir() < 0) {
        return -1;
    }
    snprintf(lease_path, sizeof(lease_path), "%s/%s.lease", UDHCP_LEASE_DIR, iface);

    log_info("dhcp_client: starting udhcpc on %s", iface);

    /* 4. fork. Parent keeps the PID; child becomes udhcpc via execlp. */
    pid_t pid = fork();
    if (pid < 0) {
        log_error("dhcp_client: fork failed: %s", strerror(errno));
        return -1;
    }

    if (pid == 0) {
        /* ----- child path -----
         * execlp replaces the child process with udhcpc. If it returns,
         * the exec failed.
         *
         * Flags:
         *   -i <iface>    bind DHCP to this interface
         *   -p <path>     write the lease to this file
         *   -f            stay in foreground (so we can track the child)
         *   -q            quiet (we have our own logging)
         *   -t 1          try once then exit; caller restarts if needed
         */
        execlp("udhcpc", "udhcpc",
               "-i", iface,
               "-p", lease_path,
               "-f", "-q", "-t", "1",
               (char *)NULL);
        log_error("dhcp_client: execlp udhcpc failed: %s", strerror(errno));
        _exit(127);  /* use _exit, not exit: don't run atexit handlers */
    }

    /* ----- parent path ----- */
    child_pid = pid;
    log_info("dhcp_client: udhcpc started (pid %d, lease=%s)", pid, lease_path);
    return 0;
}

int dhcp_cl_stop(void) {
    if (child_pid <= 0) {
        return 0;  /* not running, nothing to do — idempotent */
    }

    log_info("dhcp_client: stopping udhcpc (pid %d)", child_pid);

    /* Ask udhcpc to release the lease and exit gracefully.
     * If it's already gone (ESRCH), that's fine. */
    if (kill(child_pid, SIGTERM) < 0 && errno != ESRCH) {
        log_warn("dhcp_client: SIGTERM failed (%s), trying SIGKILL", strerror(errno));
        kill(child_pid, SIGKILL);
    }

    /* Reap the child so it doesn't linger as a zombie.
     * This blocks until udhcpc has actually exited. */
    int status;
    waitpid(child_pid, &status, 0);

    child_pid = -1;
    return 0;
}

/* --- implement these next --- */

int dhcp_cl_renew(void) {
    /* TODO: send SIGUSR1 to udhcpc to trigger a lease renewal.
     * udhcpc treats SIGUSR1 as "renew now" (SIGUSR2 would be "release").
     * Guard against child_pid <= 0 (not running).
     * Return 0 on success, -1 on failure (with a log_error). */

    if (child_pid <= 0) {
        return -1;  /* not running, cannot renew */
    }

    log_info("dhcp_client: sending SIGUSR1 to udhcpc (pid %d) to trigger lease renewal", child_pid);

    if (kill(child_pid, SIGUSR1) < 0) {
        log_error("dhcp_client: SIGUSR1 failed (%s)", strerror(errno));
        return -1;
    }

    return 0;
}

bool dhcp_cl_lease(void) {
    /* TODO: read `lease_path` and return true if it contains a valid lease.
     * Format: first line is a state ("deconfig" or "bound"), then key=value
     * lines ("ip=...", "subnet=...", "router=...", "dns=...", "lease=...").
     * Simplest check: file exists AND contains a line starting with "ip=".
     * Use fopen/fgets; close on each iteration; return false on any error. */

    FILE *f = fopen(lease_path, "r");
    if (!f) {
        log_error("dhcp_client: lease file open failed (%s)", strerror(errno));
        return false;
    }

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "ip=", 3) == 0) {
            fclose(f);
            return true;
        }
    }

    fclose(f);
    return false;
}
