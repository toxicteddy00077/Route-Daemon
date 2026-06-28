#include "log.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int failures = 0;

#define ASSERT(cond) do {                                                   \
    if (!(cond)) {                                                          \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
        failures++;                                                         \
    }                                                                       \
} while (0)

static char *read_file(const char *path, char *buf, size_t bufsz) {
    FILE *f = fopen(path, "r");
    if (f == NULL) {
        buf[0] = '\0';
        return NULL;
    }
    size_t n = fread(buf, 1, bufsz - 1, f);
    buf[n] = '\0';
    fclose(f);
    return buf;
}

int main(void) {
    const char *path = "/tmp/rd-test-log.log";
    const char *path1 = "/tmp/rd-test-log.log.1";
    unlink(path);
    unlink(path1);

    char buf1[4096], buf2[4096];

    /* --- 1. basic write produces timestamp + level + message --- */
    log_init(path, LOG_TRACE, false);
    log_info("hello %s", "world");
    log_close();
    char *out = read_file(path, buf1, sizeof(buf1));
    ASSERT(out != NULL);
    ASSERT(strstr(out, " INFO ") != NULL);
    ASSERT(strstr(out, "hello world") != NULL);
    ASSERT(strstr(out, "test_log.c:") != NULL);  /* file:line present */

    /* --- 2. level filter: LOG_INFO floor drops DEBUG, keeps INFO/WARN --- */
    unlink(path);
    log_init(path, LOG_INFO, false);
    log_debug("dropped-debug");
    log_info("kept-info");
    log_warn("kept-warn");
    log_close();
    out = read_file(path, buf1, sizeof(buf1));
    ASSERT(strstr(out, "dropped-debug") == NULL);
    ASSERT(strstr(out, "kept-info")     != NULL);
    ASSERT(strstr(out, "kept-warn")     != NULL);

    /* --- 3. log_set_level_from_string: case-insensitive, rejects junk --- */
    ASSERT(log_set_level_from_string("debug") == true);
    ASSERT(log_set_level_from_string("DEBUG") == true);
    ASSERT(log_set_level_from_string("Warn")  == true);
    ASSERT(log_set_level_from_string("trace") == true);
    ASSERT(log_set_level_from_string("fatal") == true);
    ASSERT(log_set_level_from_string("nonsense") == false);
    ASSERT(log_set_level_from_string("") == false);
    ASSERT(log_set_level_from_string(NULL) == false);

    /* --- 4. log_rotate: old content goes to .1, new file is fresh --- */
    unlink(path);
    unlink(path1);
    log_init(path, LOG_INFO, false);
    log_info("before-rotate");
    log_rotate();
    log_info("after-rotate");
    log_close();
    char *rotated = read_file(path1, buf1, sizeof(buf1));
    char *current = read_file(path, buf2, sizeof(buf2));
    ASSERT(rotated != NULL);
    ASSERT(current != NULL);
    ASSERT(strstr(rotated, "before-rotate") != NULL);
    ASSERT(strstr(rotated, "after-rotate")  == NULL);
    ASSERT(strstr(current, "after-rotate")  != NULL);
    ASSERT(strstr(current, "before-rotate") == NULL);

    /* --- 5. log_init with no path leaves fp NULL, writes go nowhere --- */
    log_close();
    unlink(path);
    log_init(NULL, LOG_INFO, false);
    log_info("silent");
    /* if this crashes or asserts, the test fails; just close cleanly */
    log_close();
    out = read_file(path, buf1, sizeof(buf1));
    ASSERT(out == NULL);

    /* --- 6. fatal always emits regardless of level floor --- */
    unlink(path);
    log_init(path, LOG_FATAL, false);  /* floor = FATAL: only FATAL passes */
    log_info("filtered");
    log_warn("filtered");
    log_fatal("kept-fatal");
    log_close();
    out = read_file(path, buf1, sizeof(buf1));
    ASSERT(strstr(out, "filtered")  == NULL);
    ASSERT(strstr(out, "kept-fatal") != NULL);

    /* --- 7. log_parse_level: same as log_set_level_from_string but non-mutating --- */
    log_levels out_level;
    ASSERT(log_parse_level("error", &out_level) == true);
    ASSERT(out_level == LOG_ERROR);
    ASSERT(log_parse_level("trace", &out_level) == true);
    ASSERT(out_level == LOG_TRACE);
    ASSERT(log_parse_level("nope",  &out_level) == false);
    ASSERT(log_parse_level(NULL,    &out_level) == false);

    unlink(path);
    unlink(path1);

    if (failures == 0) {
        printf("test_log: all checks passed\n");
        return 0;
    }
    fprintf(stderr, "test_log: %d failure(s)\n", failures);
    return 1;
}
