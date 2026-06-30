#define _POSIX_C_SOURCE 200809L

#include "config.h"
#include "log.h"

#include <json-c/json.h>
#include <pthread.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <sys/stat.h>

static rd_config *current_cfg;
static pthread_mutex_t cfg_mutex = PTHREAD_MUTEX_INITIALIZER;
static char last_path[256];

static void defaults(rd_config *c) {
    c->log_level = LOG_INFO;
    c->log_to_stderr = false;
    c->daemonize = true;

    /* network roles — left empty by default; operator MUST set in config
     * (Phase 8 entrypoint can auto-detect from /proc/net/route). */
    snprintf(c->lan_address,      sizeof(c->lan_address),      "192.168.50.1");
    snprintf(c->lan_subnet,       sizeof(c->lan_subnet),       "192.168.50.0/24");
    snprintf(c->dhcp_range_start, sizeof(c->dhcp_range_start), "192.168.50.10");
    snprintf(c->dhcp_range_end,   sizeof(c->dhcp_range_end),   "192.168.50.250");
    snprintf(c->dns_servers[0],   sizeof(c->dns_servers[0]),   "1.1.1.1");

    c->wifi_channel = 6;
    snprintf(c->wifi_country, sizeof(c->wifi_country), "IN");
    snprintf(c->wifi_ssid, sizeof(c->wifi_ssid), "Airtel_KHVSinghAirtel");
    snprintf(c->wifi_password, sizeof(c->wifi_password), "maan090405#");
    snprintf(c->dhcp_lease_time, sizeof(c->dhcp_lease_time), "12h");

    snprintf(c->log_path, sizeof(c->log_path), "/var/log/route-daemon/route-daemon.log");
    snprintf(c->pid_file, sizeof(c->pid_file), "/run/route-daemon.pid");
}

//Returns 0 parsed, 1 missing , -1 invalid.
static int parse_file(const char *path, rd_config *c) {
    struct stat st;
    if (stat(path, &st) != 0) {
        if (errno == ENOENT) { defaults(c); return 1; }
        log_error("config: stat %s: %s", path, strerror(errno));
        return -1;
    }

    json_object *j = json_object_from_file(path);
    if (j == NULL) {
        log_error("config: malformed JSON in %s", path);
        return -1;
    }
    defaults(c);

    //basic parsing pattern, i'll keep addng to this
    json_object *v;
    if (json_object_object_get_ex(j, "log_level", &v)) {
        if (!json_object_is_type(v, json_type_string) ||
            !log_parse_level(json_object_get_string(v), &c->log_level)) {
            log_error("config: log_level invalid"); goto bad;
        }
    }
    if (json_object_object_get_ex(j, "log_path", &v)) {
        if (!json_object_is_type(v, json_type_string) ||
            !json_object_get_string(v)[0]) { log_error("config: log_path invalid"); goto bad; }
        snprintf(c->log_path, sizeof(c->log_path), "%s", json_object_get_string(v));
    }
    if (json_object_object_get_ex(j, "pid_file", &v)) {
        if (!json_object_is_type(v, json_type_string) ||
            !json_object_get_string(v)[0]) { log_error("config: pid_file invalid"); goto bad; }
        snprintf(c->pid_file, sizeof(c->pid_file), "%s", json_object_get_string(v));
    }
    if (json_object_object_get_ex(j, "log_to_stderr", &v)) {
        if (!json_object_is_type(v, json_type_boolean)) { log_error("config: log_to_stderr invalid"); goto bad; }
        c->log_to_stderr = json_object_get_boolean(v);
    }
    if (json_object_object_get_ex(j, "daemonize", &v)) {
        if (!json_object_is_type(v, json_type_boolean)) { log_error("config: daemonize invalid"); goto bad; }
        c->daemonize = json_object_get_boolean(v);
    }

    if(json_object_object_get_ex(j, "wifi_channel", &v)) {
        if (!json_object_is_type(v, json_type_int)) { log_error("config: wifi_channel invalid"); goto bad; }
        c->wifi_channel = json_object_get_int(v);
    }
    if(json_object_object_get_ex(j, "wifi_country", &v)) {
        if (!json_object_is_type(v, json_type_string) ||
            !json_object_get_string(v)[0]) { log_error("config: wifi_country invalid"); goto bad; }
        snprintf(c->wifi_country, sizeof(c->wifi_country), "%s", json_object_get_string(v));
    }
    if(json_object_object_get_ex(j, "wifi_ssid", &v)) {
        if (!json_object_is_type(v, json_type_string) ||
            !json_object_get_string(v)[0]) { log_error("config: wifi_ssid invalid"); goto bad; }
        snprintf(c->wifi_ssid, sizeof(c->wifi_ssid), "%s", json_object_get_string(v));
    }
    if(json_object_object_get_ex(j, "wifi_password", &v)) {
        if (!json_object_is_type(v, json_type_string) ||
            !json_object_get_string(v)[0]) { log_error("config: wifi_password invalid"); goto bad; }
        snprintf(c->wifi_password, sizeof(c->wifi_password), "%s", json_object_get_string(v));
    }

    if (json_object_object_get_ex(j, "wan_iface", &v)) {
        if (!json_object_is_type(v, json_type_string)) { log_error("config: wan_iface invalid"); goto bad; }
        snprintf(c->wan_iface, sizeof(c->wan_iface), "%s", json_object_get_string(v));
    }
    if (json_object_object_get_ex(j, "lan_iface", &v)) {
        if (!json_object_is_type(v, json_type_string)) { log_error("config: lan_iface invalid"); goto bad; }
        snprintf(c->lan_iface, sizeof(c->lan_iface), "%s", json_object_get_string(v));
    }
    if (json_object_object_get_ex(j, "lan_address", &v)) {
        if (!json_object_is_type(v, json_type_string)) { log_error("config: lan_address invalid"); goto bad; }
        snprintf(c->lan_address, sizeof(c->lan_address), "%s", json_object_get_string(v));
    }
    if (json_object_object_get_ex(j, "lan_subnet", &v)) {
        if (!json_object_is_type(v, json_type_string)) { log_error("config: lan_subnet invalid"); goto bad; }
        snprintf(c->lan_subnet, sizeof(c->lan_subnet), "%s", json_object_get_string(v));
    }
    if (json_object_object_get_ex(j, "dhcp_range_start", &v)) {
        if (!json_object_is_type(v, json_type_string)) { log_error("config: dhcp_range_start invalid"); goto bad; }
        snprintf(c->dhcp_range_start, sizeof(c->dhcp_range_start), "%s", json_object_get_string(v));
    }
    if (json_object_object_get_ex(j, "dhcp_range_end", &v)) {
        if (!json_object_is_type(v, json_type_string)) { log_error("config: dhcp_range_end invalid"); goto bad; }
        snprintf(c->dhcp_range_end, sizeof(c->dhcp_range_end), "%s", json_object_get_string(v));
    }
    if (json_object_object_get_ex(j, "dns_servers", &v)) {
        if (!json_object_is_type(v, json_type_array)) { log_error("config: dns_servers must be array"); goto bad; }
        size_t len = json_object_array_length(v);
        if (len > 3) len = 3;
        for (size_t i = 0; i < len; i++) {
            json_object *item = json_object_array_get_idx(v, i);
            if (!json_object_is_type(item, json_type_string)) { log_error("config: dns_servers[%zu] not string", i); goto bad; }
            snprintf(c->dns_servers[i], sizeof(c->dns_servers[i]), "%s", json_object_get_string(item));
        }
    }

    json_object_put(j);
    return 0;
bad:
    json_object_put(j);
    return -1;
}

static const char *resolve_path(const char *override) {
    if (override && override[0]) return override;
    const char *env = getenv("ROUTE_DAEMON_CONFIG");
    if (env && env[0]) return env;
    return "/etc/route-daemon/config.json";
}

static int config_swap(rd_config *new_cfg) {
    pthread_mutex_lock(&cfg_mutex);
    rd_config *old = current_cfg;
    current_cfg = new_cfg;
    pthread_mutex_unlock(&cfg_mutex);
    free(old);
    if (new_cfg == NULL) {
        return -1;
    }
    return 0;
}

// Here we create a fresh config from path
static int load_and_publish(const char *path, bool warn_missing) {
    rd_config *new_cfg = calloc(1, sizeof(*new_cfg));
    if (!new_cfg) { log_error("config: out of memory"); return -1; }

    int rc = parse_file(path, new_cfg);

    switch(rc) {
        case -1: free(new_cfg); return -1;
        case 0: log_info("config: loaded from %s", path); break;
        case 1: if (warn_missing) log_warn("config: %s not found, using defaults", path); break;
    }

    snprintf(last_path, sizeof(last_path), "%s", path);

    return config_swap(new_cfg);
}

int config_load(const char *env_path) {
    return load_and_publish(resolve_path(env_path), true);
}

int config_reload(void) {
    if (!last_path[0]) { log_error("config: reload before load"); return -1; }

    rd_config *new_cfg = calloc(1, sizeof(*new_cfg));
    if (!new_cfg) { log_error("config: out of memory"); return -1; }

    int rc = parse_file(last_path, new_cfg);
    if (rc != 0) {
        if (rc > 0) log_warn("config: %s disappeared, keeping previous", last_path);
        else        log_warn("config: reload from %s failed, keeping previous", last_path);
        free(new_cfg);
        return -1;
    }
    log_info("config: reloaded from %s", last_path);

    return config_swap(new_cfg);
}

const rd_config *config_get(void) { return current_cfg; }

void config_free(rd_config *cfg) { free(cfg); }
