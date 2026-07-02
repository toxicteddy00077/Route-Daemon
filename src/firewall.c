#define _POSIX_C_SOURCE 200809L

#include "firewall.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <netinet/in.h>
#include <sys/stat.h>
#include <sys/wait.h>

/* nftables ruleset: DNAT port-forwards + masquerade, INPUT drop with LAN mgmt + ICMP exceptions, FORWARD anti-spoof + LAN->WAN, OUTPUT accept. Anti-spoof sysctls applied alongside. Atomic apply with rollback via backup ruleset. */

#define NFT_CONF   "/var/lib/route-daemon/nftables.conf"
#define NFT_BACKUP "/var/lib/route-daemon/nftables.backup"

static bool active = false;

/* run shell command; return 0 on success, -1 on non-zero exit or fork failure. */
static int run_cmd(const char *cmd) {
    int rc = system(cmd);
    if (rc == -1) { log_error("firewall: fork: %s", strerror(errno)); return -1; }
    if (!WIFEXITED(rc) || WEXITSTATUS(rc) != 0) { log_warn("firewall: %s rc=%d", cmd, WEXITSTATUS(rc)); return -1; }
    return 0;
}

/* write a sysctl value; best-effort (log and continue on failure). */
static int write_sysctl(const char *path, const char *value) {
    FILE *f = fopen(path, "w");
    if (!f) { log_warn("firewall: sysctl %s: %s", path, strerror(errno)); return -1; }
    fprintf(f, "%s", value);
    fclose(f);
    return 0;
}

struct sysctl_pair { const char *path; const char *value; };

static int apply_antispoof(void) {
    struct sysctl_pair tbl[] = {
        {"/proc/sys/net/ipv4/conf/all/rp_filter",            "1"},
        {"/proc/sys/net/ipv4/conf/default/rp_filter",        "1"},
        {"/proc/sys/net/ipv4/conf/all/accept_source_route",  "0"},
        {"/proc/sys/net/ipv4/conf/default/accept_source_route","0"},
        {"/proc/sys/net/ipv4/conf/all/accept_redirects",     "0"},
        {"/proc/sys/net/ipv4/conf/default/accept_redirects", "0"},
        {"/proc/sys/net/ipv4/conf/all/send_redirects",        "0"},
        {"/proc/sys/net/ipv4/conf/default/send_redirects",    "0"},
        {"/proc/sys/net/ipv4/conf/all/log_martians",          "1"},
        {"/proc/sys/net/ipv4/conf/default/log_martians",      "1"},
        {"/proc/sys/net/netfilter/nf_conntrack_max",          "65536"},
        {NULL, NULL}
    };
    int err = 0;
    for (int i = 0; tbl[i].path; i++) {
        if (write_sysctl(tbl[i].path, tbl[i].value) < 0) err = -1;
    }
    return err;
}

/* map IPPROTO_* to nftables keyword. */
static const char *proto_str(int p) {
    switch (p) {
        case IPPROTO_TCP: return "tcp";
        case IPPROTO_UDP: return "udp";
        default:          return "tcp";
    }
}

/* flush old ruleset before writing new one (atomic replace). */
static void write_header(FILE *f) {
    fprintf(f, "# route-daemon firewall\nflush ruleset\n");
}

/* nat: prerouting DNAT for port forwards, postrouting masquerade on WAN. */
static void write_nat(FILE *f, const rd_config *c) {
    fprintf(f,
        "table ip nat {\n"
        "    chain prerouting {\n"
        "        type nat hook prerouting priority -100; policy accept;\n");
    for (int i = 0; i < c->port_forward_count; i++) {
        fprintf(f, "        %s dport %d dnat to %s:%d\n",
            proto_str(c->port_forwards[i].proto),
            c->port_forwards[i].ext_port,
            c->port_forwards[i].int_ip,
            c->port_forwards[i].int_port);
    }
    fprintf(f,
        "    }\n"
        "    chain postrouting {\n"
        "        type nat hook postrouting priority srcnat; policy accept;\n"
        "        oifname \"%s\" masquerade\n"
        "    }\n"
        "}\n", c->wan_iface);
}

/* input: default drop; allow loopback, established/related, rate-limited ICMP, LAN mgmt ports. */
static void write_input(FILE *f, const rd_config *c) {
    fprintf(f,
        "    chain input {\n"
        "        type filter hook input priority 0; policy drop;\n"
        "        iifname \"lo\" accept\n"
        "        ct state established,related accept\n"
        "        ip protocol icmp limit rate 4/second accept\n");
    if (c->management_port_count > 0) {
        fprintf(f, "        iifname \"%s\" tcp dport {", c->lan_iface);
        for (int i = 0; i < c->management_port_count; i++) {
            fprintf(f, "%s%d", i ? ", " : " ", c->management_ports[i]);
        }
        fprintf(f, " } accept\n");
    }
    fprintf(f,
        "        log prefix \"INPUT-DROP: \" limit rate 10/minute\n"
        "        drop\n"
        "    }\n");
}

/* forward: default drop; drop RFC1918 sources from WAN, established/related pass, LAN->WAN accept, WAN->LAN only established/related (DNAT'd replies). */
static void write_forward(FILE *f, const rd_config *c) {
    fprintf(f,
        "    chain forward {\n"
        "        type filter hook forward priority 0; policy drop;\n"
        "        iifname \"%s\" ip saddr 192.168.0.0/16 drop\n"
        "        iifname \"%s\" ip saddr 10.0.0.0/8 drop\n"
        "        iifname \"%s\" ip saddr 172.16.0.0/12 drop\n"
        "        iifname \"%s\" ip saddr 127.0.0.0/8 drop\n"
        "        ct state established,related accept\n"
        "        iifname \"%s\" oifname \"%s\" accept\n"
        "        iifname \"%s\" oifname \"%s\" ct state established,related accept\n"
        "        log prefix \"FWD-DROP: \" limit rate 10/minute\n"
        "        drop\n"
        "    }\n",
        c->wan_iface, c->wan_iface, c->wan_iface, c->wan_iface,
        c->lan_iface, c->wan_iface,
        c->wan_iface, c->lan_iface);
}

/* filter table: input + forward + output (output open; daemon's own traffic isn't restricted). */
static void write_filter(FILE *f, const rd_config *c) {
    fprintf(f, "table ip filter {\n");
    write_input(f, c);
    write_forward(f, c);
    fprintf(f,
        "    chain output {\n"
        "        type filter hook output priority 0; policy accept;\n"
        "    }\n"
        "}\n");
}

/* write the full ruleset to NFT_CONF. */
static int generate_ruleset(const rd_config *cfg) {
    FILE *f = fopen(NFT_CONF, "w");
    if (!f) { log_error("firewall: fopen %s: %s", NFT_CONF, strerror(errno)); return -1; }
    write_header(f);
    write_nat(f, cfg);
    write_filter(f, cfg);
    fclose(f);
    return 0;
}

/* check nft binary is on PATH. */
int firewall_init(void) {
    if (access("/sbin/nft", X_OK) != 0 && access("/usr/sbin/nft", X_OK) != 0) {
        log_error("firewall: nft binary not found");
        return -1;
    }
    return 0;
}

int firewall_apply(const rd_config *cfg) {
    if (!cfg) return -1;
    run_cmd("nft list ruleset > " NFT_BACKUP);                              /* save current for rollback */
    if (generate_ruleset(cfg) < 0) return -1;
    if (run_cmd("nft -f " NFT_CONF) < 0) {                                /* apply; restore on fail */
        log_error("firewall: apply failed, rolling back");
        run_cmd("nft -f " NFT_BACKUP);
        return -1;
    }
    apply_antispoof();                                                     /* rp_filter, conntrack, martians */
    active = true;
    log_info("firewall: applied (forwards=%d, mgmt_ports=%d, antispoof)",
             cfg->port_forward_count, cfg->management_port_count);
    return 0;
}

bool firewall_is_active(void) { return active; }

/* drop our tables; idempotent. */
int firewall_flush(void) {
    run_cmd("nft delete table ip filter 2>/dev/null");
    run_cmd("nft delete table ip nat 2>/dev/null");
    active = false;
    log_info("firewall: flushed");
    return 0;
}
