#define _POSIX_C_SOURCE 200809L

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <getopt.h> //basic cli lib
#include <errno.h>

#include "log.h"
#include "config.h"
#include "daemon.h"
#include "interface.h"
#include "dhcp_cl.h"
#include "dhcp_sv.h"
#include "firewall.h"
#include "wifi_ap.h"
#include "shaper.h"
#include "health.h"

struct cli_opts {
    const char *config_path;  // -c
    const char *pid_file;     // -p
    const char *log_level;    // -l (string; parsed via log_parse_level)
    bool        foreground;   // -f
};

static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s [-c config] [-p pidfile] [-l level] [-f]\n"
        "  -c, --config PATH    config file (default: $ROUTE_DAEMON_CONFIG or /etc/route-daemon/config.json)\n"
        "  -p, --pid-file PATH  pid file (default: from config)\n"
        "  -l, --log-level LVL  trace|debug|info|warn|error|fatal (default: from config)\n"
        "  -f, --foreground     don't daemonize, log to stderr\n"
        "  -h, --help           show this help\n",
        prog);
}

static int parse_args(int argc, char *argv[], struct cli_opts *o) {
    o->config_path = NULL;
    o->pid_file    = NULL;
    o->log_level   = NULL;
    o->foreground  = false;

    static struct option longopts[] = {
        {"config",     required_argument, NULL, 'c'},
        {"pid-file",   required_argument, NULL, 'p'},
        {"log-level",  required_argument, NULL, 'l'},
        {"foreground", no_argument,       NULL, 'f'},
        {"help",       no_argument,       NULL, 'h'},
        {NULL, 0, NULL, 0},
    };

    int c;
    opterr = 0;  // we print our own errors
    while ((c = getopt_long(argc, argv, "c:p:l:fh", longopts, NULL)) != -1) {
        switch (c) {
        case 'c': o->config_path = optarg; break;
        case 'p': o->pid_file    = optarg; break;
        case 'l': o->log_level   = optarg; break;
        case 'f': o->foreground  = true;   break;
        case 'h': usage(argv[0]); return 1;  // 1 == "showed help, exit 0"
        case '?':
        default:
            fprintf(stderr, "unknown or malformed option: -%c\n", optopt ? optopt : '?');
            usage(argv[0]);
            return -1;
        }
    }
    return 0;
}

static int enable_ip_forward(void) {
    FILE *f = fopen("/proc/sys/net/ipv4/ip_forward", "w");
    if (f == NULL) {
        log_error("ip_forward: cannot open /proc/sys/net/ipv4/ip_forward: %s",
                  strerror(errno));
        return -1;
    }
    if (fprintf(f, "1") < 1 || fclose(f) != 0) {
        log_error("ip_forward: write failed");
        return -1;
    }
    log_info("ip_forward enabled");
    return 0;
}

int main(int argc, char *argv[]) {
    struct cli_opts opts;
    int             exit_code = 0;

    switch (parse_args(argc, argv, &opts)) {
        case -1:
            return 64;  // EX_USAGE
        case 1:
            return 0;   // --help
        default:
            break;
    }

    if (config_load(opts.config_path) < 0) {
        fprintf(stderr, "config: failed to load\n");
        return 78;  // EX_CONFIG
    }

    const rd_config *cfg = config_get();
    if (cfg == NULL) {
        fprintf(stderr, "config: no config after load\n");
        return 78;
    }

    /* CLI overrides: -p and -l mutate the loaded config struct.
     * Safe because this is the boot path, not the reload path (no other
     * readers can observe a half-mutated config here). */
    rd_config boot = *cfg;  // local copy so we don't mutate the live singleton
    if (opts.pid_file != NULL) {
        snprintf(boot.pid_file, sizeof(boot.pid_file), "%s", opts.pid_file);
    }
    if (opts.log_level != NULL) {
        if (!log_parse_level(opts.log_level, &boot.log_level)) {
            fprintf(stderr, "invalid --log-level: %s\n", opts.log_level);
            return 64;
        }
    }
    if (opts.foreground) {
        boot.log_to_stderr = true;
        boot.daemonize     = false;
    }

    //start logging
    log_init(boot.log_path, boot.log_level, boot.log_to_stderr);
    log_info("starting, version=%s", DAEMON_VERSION);

    if (interface_init() < 0) {
        log_error("interface_init failed");
        exit_code = 1;
        goto cleanup;
    }

    if (enable_ip_forward() < 0) {
        exit_code = 1;
        goto cleanup;
    }

    /* Phase 1 Step 5a: bring up LAN iface with our address, and prepare WAN
     * for udhcpc. Skip if the operator hasn't configured roles yet. */
    {
        char wan[32], lan[32];
        interface_get_roles(wan, sizeof(wan), lan, sizeof(lan));

        if (lan[0] != '\0') {
            if (iface_up(lan) < 0) {
                log_error("iface_up(%s) failed", lan);
                exit_code = 1;
                goto cleanup;
            }
            iface_flush_addrs(lan);  /* idempotent: clean slate before adding */
            char cidr[64];
            snprintf(cidr, sizeof(cidr), "%s/24", boot.lan_address);
            if (iface_add_addr(lan, cidr) < 0) {
                log_error("iface_add_addr(%s, %s) failed", lan, cidr);
                exit_code = 1;
                goto cleanup;
            }
        } else {
            log_warn("lan_iface not set in config; skipping LAN setup");
        }

        if (wan[0] != '\0') {
            if (iface_up(wan) < 0) {
                log_warn("iface_up(%s) failed (WAN may already be up)", wan);
            }
            iface_flush_addrs(wan);  /* best-effort: clean slate for udhcpc */
        } else {
            log_warn("wan_iface not set in config; skipping WAN setup");
        }
    }

    /* Phase 1 Step 6: start the WAN DHCP client (udhcpc on the WAN iface).
     * Failure here is non-fatal — the daemon can still run with a static
     * address; we just log and continue. */
    {
        char wan[32];
        interface_get_roles(wan, sizeof(wan), NULL, 0);
        if (wan[0] != '\0') {
            if (dhcp_cl_start(wan) < 0) {
                log_warn("WAN DHCP client failed to start; continuing without it");
            }
        }
    }

    /* Phase 1 Step 7: WiFi AP (hostapd) on the LAN iface. */
    {
        char lan[32];
        interface_get_roles(NULL, 0, lan, sizeof(lan));
        if (lan[0] != '\0') {
            if (wifi_ap_start(&boot) < 0) {
                log_warn("wifi_ap_start failed; AP not broadcasting");
            }
        }
    }

    /* Phase 1 Step 5b: apply nftables masquerade + forward rules. */
    if (firewall_apply(&boot) < 0) {
        log_error("firewall_apply failed");
        exit_code = 1;
        goto cleanup;
    }

    /* Phase 3: ingress/egress shaping + bufferbloat mitigation. */
    if (shaper_init(&boot) < 0) {
        log_warn("shaper_init failed; continuing without shaping");
    }

    /* Phase 1 Step 7b: LAN DHCP server (dnsmasq) — only after the
     * firewall is up, so the gateway is reachable when clients get leases. */
    {
        char lan[32];
        interface_get_roles(NULL, 0, lan, sizeof(lan));
        if (lan[0] != '\0') {
            if (dhcp_sv_start(&boot) < 0) {
                log_warn("dhcp_server failed to start; clients won't get leases");
            }
        }
    }

    if (daemon_init(&boot) < 0) {
        log_error("daemon_init failed");
        exit_code = 1;
        goto cleanup;
    }

    if (boot.daemonize) {
        if (daemonize() < 0) {
            exit_code = 1;
            goto cleanup;
        }
    }

    if (pid_file_create(boot.pid_file) < 0) {
        log_error("pid_file_create failed (another instance running?)");
        exit_code = 1;
        goto cleanup;
    }

    if (signal_setup() < 0) {
        log_error("signal_setup failed");
        exit_code = 1;
        goto cleanup;
    }

    log_info("entering main loop");
    health_init();
    daemon_loop();   // blocks until SIGTERM/SIGINT

cleanup:
    shaper_teardown();
    dhcp_sv_stop();
    firewall_flush();
    wifi_ap_stop();
    dhcp_cl_stop();
    pid_file_remove();
    log_info("shutdown complete");
    log_close();
    return exit_code;
}
