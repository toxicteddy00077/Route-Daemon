#ifndef DAEMON_H
#define DAEMON_H

#include <stdbool.h>
#include "config.h"

typedef struct {
    bool shutd_req;
    bool reload_req;
    int signal_fd;
    int pid_fd;
    char pid_file[256];
} rd_daemon;

rd_daemon* daemon_init(rd_config *config);

int daemonize(void);  // double-fork into background; call before pid_file_create

int pid_file_create(rd_daemon *daemon, const char* path);

int signal_setup(rd_daemon *daemon);

int daemon_loop(rd_daemon *daemon); //this is core loop where signals are handled

int pid_file_remove(rd_daemon *daemon);

#endif
