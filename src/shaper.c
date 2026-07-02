#define _POSIX_C_SOURCE 200809L

/* cake on WAN egress (shapes uploads) + cake on ifb0 (shapes downloads) via tc ingress redirect. Egress+ingress shaping + AQM = bufferbloat mitigation. */

#include "shaper.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

static int run(const char *cmd) {
    int rc = system(cmd);
    if (rc == -1 || !WIFEXITED(rc) || WEXITSTATUS(rc) != 0) {
        log_warn("shaper: %s failed", cmd);
        return -1;
    }
    return 0;
}

static int cake(const char *iface, int kbps) {
    char c[256];
    snprintf(c, sizeof(c), "tc qdisc replace dev %s root cake bandwidth %dkbit nat", iface, kbps);
    return run(c);
}

static int ingress_to_ifb(const char *wan, int kbps) {
    char c[512];
    snprintf(c, sizeof(c),
        "ip link add ifb0 type ifb 2>/dev/null; "
        "ip link set ifb0 up; "
        "tc qdisc add dev %s ingress; "
        "tc filter add dev %s parent ffff: protocol ip u32 match u32 0 0 "
        "action mirred egress redirect dev ifb0; "
        "tc qdisc replace dev ifb0 root cake bandwidth %dkbit nat",
        wan, wan, kbps);
    return run(c);
}

static char saved_wan[32];
static bool active = false;

int shaper_init(const rd_config *cfg) {
    if (!cfg || cfg->wan_iface[0] == '\0') return -1;
    if (cfg->wan_bandwidth_up <= 0 || cfg->wan_bandwidth_down <= 0) {
        log_info("shaper: bandwidth unset, skipping");
        return 0;
    }
    snprintf(saved_wan, sizeof(saved_wan), "%s", cfg->wan_iface);
    run("modprobe sch_cake 2>/dev/null");
    int r = 0;
    r |= cake(cfg->wan_iface, cfg->wan_bandwidth_up);
    r |= ingress_to_ifb(cfg->wan_iface, cfg->wan_bandwidth_down);
    if (r < 0) {
        log_warn("shaper: some tc ops failed");
        return -1;
    }
    log_info("shaper: cake applied up=%d down=%d kbps",
             cfg->wan_bandwidth_up, cfg->wan_bandwidth_down);
    active = true;
    return 0;
}

int shaper_teardown(void) {
    if (saved_wan[0] == '\0') return 0;
    char c[256];
    snprintf(c, sizeof(c),
        "tc qdisc del dev %s root 2>/dev/null; "
        "tc qdisc del dev %s ingress 2>/dev/null; "
        "tc qdisc del dev ifb0 root 2>/dev/null; "
        "ip link del ifb0 2>/dev/null",
        saved_wan, saved_wan);
    active = false;
    return run(c);
}

bool shaper_is_active(void) { return active; }
