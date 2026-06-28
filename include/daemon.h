#ifndef DAEMON_H
#define DAEMON_H

#include "config.h"

int daemon_init(rd_config *config);   // initialize module state from config
int daemonize(void);                  // double-fork into background
int pid_file_create(const char *path); // open + flock + write pid
int signal_setup(void);               // block signals, create signalfd
int daemon_loop(void);                // runs until SIGTERM/SIGINT
int pid_file_remove(void);            // unlink + close

#endif
