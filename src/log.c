#define _POSIX_C_SOURCE 200809L

#include "log.h"
#include <pthread.h>
#include <stdarg.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>

static FILE* log_fp = NULL;
static log_levels cur_level = LOG_INFO;
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool log_to_stderr = false;
static char log_path[PATH_MAX];

const char *LOG_LEVEL_NAMES[] = {
    "TRACE",
    "DEBUG",
    "INFO",
    "WARN",
    "ERROR",
    "FATAL",
};

const int LOG_LEVEL_COUNT = sizeof(LOG_LEVEL_NAMES) / sizeof(LOG_LEVEL_NAMES[0]);

// Initialization, setup, and helpers.
void log_init(const char *path, log_levels min_level, bool to_stderr) {
    if (path != NULL) {
        strncpy(log_path, path, sizeof(log_path) - 1);
        log_path[sizeof(log_path) - 1] = '\0';
        log_fp = fopen(path, "a");
        if (log_fp == NULL) {
            fprintf(stderr, "log: fopen %s: %s\n", path, strerror(errno));
        }
    }
    cur_level = min_level;
    log_to_stderr = to_stderr;
}

void log_close(void) {
    if (log_fp != NULL) {
        fclose(log_fp);
        log_fp = NULL;
    }
    pthread_mutex_destroy(&log_mutex);
    cur_level = LOG_INFO;
    log_to_stderr = false;
    log_path[0] = '\0';
}

void log_set_level(log_levels level) {
    cur_level = level;
}

bool log_set_level_from_string(const char *level) {
    log_levels lvl;
    if (!log_parse_level(level, &lvl)) {
        return false;
    }
    cur_level = lvl;
    return true;
}

bool log_parse_level(const char *s, log_levels *out) {
    if (s == NULL || out == NULL) return false;
    for (int i = 0; i < LOG_LEVEL_COUNT; i++) {
        if (strcasecmp(s, LOG_LEVEL_NAMES[i]) == 0) {
            *out = (log_levels)i;
            return true;
        }
    }
    return false;
}

void log_rotate(void) {
    if (log_path[0] == '\0') {
        return;
    }
    if (log_fp != NULL) {
        fclose(log_fp);
        log_fp = NULL;
    }
    char rot[PATH_MAX + 16];
    snprintf(rot, sizeof(rot), "%s.1", log_path);
    rename(log_path, rot);
    log_fp = fopen(log_path, "a");
    if (log_fp == NULL) {
        fprintf(stderr, "log: fopen %s: %s\n", log_path, strerror(errno));
    }
}

// Core logger.
void write_log(log_levels level, const char *file, int line, const char *fmt, ...) {
    if (level < cur_level) {
        return;
    }
    if ((int)level < 0 || (int)level >= LOG_LEVEL_COUNT) {
        return;
    }

    char ts[32];
    time_t now = time(NULL);
    struct tm tm_buf;
    localtime_r(&now, &tm_buf);
    strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%S%z", &tm_buf);

    char msg[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);

    pthread_mutex_lock(&log_mutex);
    if (log_fp != NULL) {
        fprintf(log_fp, "%s %-5s %s:%d %s\n", ts, LOG_LEVEL_NAMES[level], file, line, msg);
        fflush(log_fp);
    }
    if (log_to_stderr) {
        fprintf(stderr, "%s %-5s %s:%d %s\n", ts, LOG_LEVEL_NAMES[level], file, line, msg);
    }
    pthread_mutex_unlock(&log_mutex);
}
