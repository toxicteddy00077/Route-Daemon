#define _POSIX_C_SOURCE 200809L

#include "daemon.h"
#include "log.h"
#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/signalfd.h>
#include <sys/file.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <poll.h>

static bool shutd_req;
static bool reload_req;
static int  signal_fd = -1;
static int  pid_fd    = -1;
static char pid_file[256];

int daemon_init(rd_config *config) {
    if (config == NULL) {
        return -1;
    }
    shutd_req  = false;
    reload_req = false;
    signal_fd  = -1;
    pid_fd     = -1;
    if (config->pid_file[0]) {
        snprintf(pid_file, sizeof(pid_file), "%s", config->pid_file);
    } else {
        pid_file[0] = '\0';
    }
    return 0;
}

int daemonize(void) {
    pid_t pid;

    pid = fork();
    switch (pid) {
    case -1:
        log_error("daemonize: first fork failed: %s", strerror(errno));
        return -1;
    case 0:
        break;
    default:
        _exit(EXIT_SUCCESS);
    }

    if (setsid() < 0) {
        log_error("daemonize: setsid failed: %s", strerror(errno));
        return -1;
    }

    pid = fork();
    switch (pid) {
    case -1:
        log_error("daemonize: second fork failed: %s", strerror(errno));
        return -1;
    case 0:
        break;
    default:
        _exit(EXIT_SUCCESS);
    }

    if (chdir("/") < 0) {
        return -1;
    }
    umask(0);

    int devnull = open("/dev/null", O_RDWR);
    if (devnull < 0) {
        return -1;
    }
    dup2(devnull, STDIN_FILENO);
    dup2(devnull, STDOUT_FILENO);
    dup2(devnull, STDERR_FILENO);
    if (devnull > STDERR_FILENO) {
        close(devnull);
    }
    return 0;
}

int pid_file_create(const char *path) {
    if (path == NULL) {
        return -1;
    }

    int fd = open(path, O_WRONLY | O_CREAT | O_CLOEXEC, 0644);
    if (fd < 0) {
        return -1;
    }

    if (flock(fd, LOCK_EX | LOCK_NB) < 0) {
        close(fd);
        return -1;
    }

    if (ftruncate(fd, 0) < 0) {
        close(fd);
        return -1;
    }

    char buf[32];
    int len = snprintf(buf, sizeof(buf), "%d\n", (int)getpid());
    if (write(fd, buf, (size_t)len) != len) {
        close(fd);
        return -1;
    }

    pid_fd = fd;
    snprintf(pid_file, sizeof(pid_file), "%s", path);
    return 0;
}

int signal_setup(void) {
    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) return -1;
    if (signal(SIGTTIN, SIG_IGN) == SIG_ERR) return -1;
    if (signal(SIGTTOU, SIG_IGN) == SIG_ERR) return -1;

    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGHUP);
    if (sigprocmask(SIG_BLOCK, &mask, NULL) < 0) {
        return -1;
    }

    int fd = signalfd(-1, &mask, SFD_CLOEXEC);
    if (fd < 0) {
        return -1;
    }

    signal_fd = fd;
    return 0;
}

int daemon_loop(void) {
    if (signal_fd < 0) {
        return -1;
    }

    struct pollfd pfd = { .fd = signal_fd, .events = POLLIN };

    while (!shutd_req) {
        int ret = poll(&pfd, 1, 1000);
        if (ret < 0) {
            if (errno == EINTR) {
                continue;
            }
            log_error("daemon: poll failed: %s", strerror(errno));
            return -1;
        }
        if (ret == 0) {
            continue;
        }

        if (pfd.revents & POLLIN) {
            struct signalfd_siginfo si;
            ssize_t n = read(signal_fd, &si, sizeof(si));
            if (n != sizeof(si)) {
                if (n < 0 && errno != EAGAIN) {
                    log_error("daemon: signalfd read failed: %s", strerror(errno));
                    return -1;
                }
                continue;
            }

            switch (si.ssi_signo) {
            case SIGTERM:
            case SIGINT:
                log_info("daemon: received %s, shutting down",
                         si.ssi_signo == SIGTERM ? "SIGTERM" : "SIGINT");
                shutd_req = true;
                break;
            case SIGHUP:
                log_info("daemon: received SIGHUP, reloading");
                reload_req = true;
                break;
            default:
                break;
            }
        } else if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
            log_error("daemon: signalfd error (revents=0x%x)", pfd.revents);
            return -1;
        }

        if (reload_req) {
            reload_req = false;
            if (config_reload() == 0) {
                const rd_config *cfg = config_get();
                if (cfg != NULL) {
                    log_set_level(cfg->log_level);
                }
                log_rotate();
                log_info("daemon: reload complete");
            } else {
                log_warn("daemon: reload failed, previous config retained");
            }
        }
    }

    log_info("daemon: loop exiting");
    return 0;
}

int pid_file_remove(void) {
    if (pid_file[0] != '\0') {
        unlink(pid_file);
    }
    if (pid_fd >= 0) {
        close(pid_fd);
        pid_fd = -1;
    }
    return 0;
}
