#define _GNU_SOURCE
#include "acia.h"
#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/poll.h>
#include <sys/eventfd.h>

typedef struct ACIA_Cxt_s
{
    bool init;
    int term_fd;
    int shutdown_fd;
    pthread_t thread;
} ACIA_Cxt_t;

static ACIA_Cxt_t ACIA_Cxt;

void *terminal_thread(void *p)
{
    struct pollfd fds[2];
    bool shutdown = false;

    grantpt(ACIA_Cxt.term_fd);
    unlockpt(ACIA_Cxt.term_fd);

    fds[0].events = POLLIN;
    fds[0].fd = ACIA_Cxt.term_fd;
    fds[1].events = POLLIN;
    fds[1].fd = ACIA_Cxt.shutdown_fd;

    while(!shutdown)
    {
        int r;

        r = poll(fds, 2, -1);

        if(r > 0)
        {
            if(fds[1].revents == POLLIN)
            {
                shutdown = true;
                continue;
            }
            else if(fds[0].revents == POLLIN)
            {
                /* Handle input data. */
            }
        }
    }

    return NULL;
}

bool acia_init(void)
{
    int fd;
    char name[64];

    fd = open("/dev/ptmx", O_RDWR);

    if(fd < 0)
    {
        fprintf(stderr, "Unable to open ptmx: %s\n", strerror(errno));
        return false;
    }

    if(ptsname_r(fd, name, sizeof(name)) < 0)
    {
        fprintf(stderr, "Unable to query pts name: %s\n", strerror(errno));
        close(fd);
        return false;
    }

    ACIA_Cxt.term_fd = fd;

    ACIA_Cxt.shutdown_fd = eventfd(0, 0);

    if(ACIA_Cxt.shutdown_fd < 0)
    {
        fprintf(stderr, "eventfd failure %s\n", strerror(errno));
        close(fd);
        return false;
    }

    if(pthread_create(&ACIA_Cxt.thread, NULL, terminal_thread, NULL) < 0)
    {
        fprintf(stderr, "Unable to start ACIA thread: %s\n", strerror(errno));
        close(ACIA_Cxt.term_fd);
        close(ACIA_Cxt.shutdown_fd);
        return false;
    }

    printf("Pts name: %s\n", name);

    return true;
}

void acia_write(uint8_t reg, uint8_t val)
{
    if(reg == 0)
    {
        write(ACIA_Cxt.term_fd, &val, 1);
    }
}

uint8_t acia_read(uint8_t reg)
{
    return 0;
}
