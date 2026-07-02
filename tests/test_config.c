#include "config.h"
#include "log.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

static int failures = 0;

#define ASSERT(cond) do {                                                   \
    if (!(cond)) {                                                          \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
        failures++;                                                         \
    }                                                                       \
} while (0)

static void write_file(const char *path, const char *content) {
    FILE *f = fopen(path, "w");
    if (f != NULL) {
        fputs(content, f);
        fclose(f);
    }
}

int main(void) {
    const char *p = "/tmp/rd-test-cfg.json";
    unlink(p);

    // silence log_warn/log_error from the config module
    log_init("/tmp/rd-test-cfg-log.log", LOG_INFO, false);

    // --- 1. missing file: returns 0, defaults applied ---
    ASSERT(config_load("/tmp/rd-nonexistent-cfg-XXXXX.json") == 0);
    const rd_config *c = config_get();
    ASSERT(c != NULL);
    ASSERT(c->log_level    == LOG_INFO);
    ASSERT(strcmp(c->log_path, "/var/log/route-daemon/route-daemon.log") == 0);
    ASSERT(c->log_to_stderr == false);
    ASSERT(strcmp(c->pid_file, "/run/route-daemon.pid") == 0);
    ASSERT(c->daemonize    == true);

    // --- 2. partial config: missing keys keep defaults ---
    write_file(p, "{\"log_level\":\"debug\",\"daemonize\":false}\n");
    ASSERT(config_load(p) == 0);
    c = config_get();
    ASSERT(c != NULL);
    ASSERT(c->log_level == LOG_DEBUG);
    ASSERT(c->daemonize == false);
    ASSERT(c->log_to_stderr == false);                          // default
    ASSERT(strcmp(c->pid_file, "/run/route-daemon.pid") == 0); // default

    // --- 3. full config: every field read ---
    write_file(p,
        "{\"log_level\":\"warn\","
        "\"log_path\":\"/tmp/x.log\","
        "\"log_to_stderr\":true,"
        "\"pid_file\":\"/tmp/x.pid\","
        "\"daemonize\":true}\n");
    ASSERT(config_load(p) == 0);
    c = config_get();
    ASSERT(c != NULL);
    ASSERT(c->log_level     == LOG_WARN);
    ASSERT(strcmp(c->log_path, "/tmp/x.log") == 0);
    ASSERT(c->log_to_stderr == true);
    ASSERT(strcmp(c->pid_file, "/tmp/x.pid") == 0);
    ASSERT(c->daemonize     == true);

    // --- 4. malformed JSON: returns -1, previous config kept ---
    write_file(p, "{ broken");
    ASSERT(config_load(p) == -1);
    c = config_get();
    ASSERT(c != NULL);
    ASSERT(c->log_level == LOG_WARN);  // unchanged from test 3

    // --- 5. unknown level name: returns -1 ---
    write_file(p, "{\"log_level\":\"silly\"}\n");
    ASSERT(config_load(p) == -1);

    // --- 6. wrong type (log_level as int): returns -1 ---
    write_file(p, "{\"log_level\":42}\n");
    ASSERT(config_load(p) == -1);

    // --- 7. wrong type (daemonize as string): returns -1 ---
    write_file(p, "{\"daemonize\":\"yes\"}\n");
    ASSERT(config_load(p) == -1);

    // --- 8. empty string in path: returns -1 ---
    write_file(p, "{\"log_path\":\"\"}\n");
    ASSERT(config_load(p) == -1);

    // --- 9. unknown keys: silently ignored, load succeeds ---
    write_file(p, "{\"log_level\":\"info\",\"future_key\":123}\n");
    ASSERT(config_load(p) == 0);
    c = config_get();
    ASSERT(c->log_level == LOG_INFO);

    // --- 10. reload: good change applies, bad change keeps previous ---
    write_file(p, "{\"log_level\":\"error\"}\n");
    ASSERT(config_load(p) == 0);
    c = config_get();
    ASSERT(c->log_level == LOG_ERROR);

    // bad change: reload returns -1, config unchanged
    write_file(p, "{ broken");
    ASSERT(config_reload() == -1);
    c = config_get();
    ASSERT(c->log_level == LOG_ERROR);

    // good change: reload returns 0, config updated
    write_file(p, "{\"log_level\":\"trace\"}\n");
    ASSERT(config_reload() == 0);
    c = config_get();
    ASSERT(c->log_level == LOG_TRACE);

    // --- 11. reload: file disappeared returns -1, keeps previous ---
    unlink(p);
    ASSERT(config_reload() == -1);
    c = config_get();
    ASSERT(c->log_level == LOG_TRACE);  // still the trace config

    unlink(p);
    unlink("/tmp/rd-test-cfg-log.log");
    log_close();

    if (failures == 0) {
        printf("test_config: all checks passed\n");
        return 0;
    }
    fprintf(stderr, "test_config: %d failure(s)\n", failures);
    return 1;
}
