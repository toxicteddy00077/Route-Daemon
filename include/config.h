#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>
#include "log.h"

/* Runtime configuration. Process-wide singleton; readers use config_get(),
 * the SIGHUP path replaces it atomically via config_reload(). Returned
 * structs are read-only — never mutate in place, build a fresh one and
 * swap. */
typedef struct {
    log_levels log_level;        // Minimum severity written to logs.
    char       log_path[256];
    bool       log_to_stderr;   // Also mirror log lines to stderr.
    char       pid_file[256];
    bool       daemonize;
} rd_config;

int config_load(const char *env_path);

const rd_config *config_get(void);

 // Re-read the last-loaded path and atomically swap it in.
 // On any failure the previous config stays active.
int config_reload(void);

void config_free(rd_config *cfg);

#endif
