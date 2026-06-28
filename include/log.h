#ifndef LOG_H
#define LOG_H

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>

typedef enum {
    LOG_TRACE,
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR,
    LOG_FATAL
} log_levels;

#define log_info(...) write_log(LOG_INFO, __FILE__, __LINE__, __VA_ARGS__)
#define log_error(...) write_log(LOG_ERROR, __FILE__, __LINE__, __VA_ARGS__)
#define log_debug(...) write_log(LOG_DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#define log_trace(...) write_log(LOG_TRACE, __FILE__, __LINE__, __VA_ARGS__)
#define log_fatal(...) write_log(LOG_FATAL, __FILE__, __LINE__, __VA_ARGS__)
#define log_warn(...) write_log(LOG_WARN, __FILE__, __LINE__, __VA_ARGS__)

//Initialize, setup and helpers
void log_init(const char *path, log_levels min_level, bool to_stderr);

void log_close(void);

void log_set_level(log_levels level);

bool log_set_level_from_string(const char *level);

bool log_parse_level(const char *s, log_levels *out);

void log_rotate(void);


//core logger
void write_log(log_levels level, const char *file, int line, const char *fmt, ...);

#endif
