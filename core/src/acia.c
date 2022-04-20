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
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/types.h>

typedef struct ACIA_Cxt_s
{
    bool init;
    int server_fd;
    int term_fd;
    int shutdown_fd;
    pthread_t thread;
    pthread_mutex_t mutex;
} ACIA_Cxt_t;

static ACIA_Cxt_t ACIA_Cxt;

void *terminal_thread(void *p)
{
    struct pollfd fds[2];
    bool shutdown = false;
    uint8_t buf[256];
    int newsock;

    while(!shutdown)
    {
        int r;

        fds[0].events = POLLIN;
        fds[0].fd = ACIA_Cxt.server_fd;
        fds[1].events = POLLIN;
        fds[1].fd = ACIA_Cxt.shutdown_fd;

        r = poll(fds, 2, -1);

        if(r > 0)
        {
            if(fds[1].revents == POLLIN)
            {
                printf("shutdown\n");
                shutdown = true;
                continue;
            }
            else if(fds[0].revents == POLLIN)
            {
                newsock = accept4(ACIA_Cxt.server_fd, NULL, NULL, SOCK_NONBLOCK);

                if(newsock < 0)
                {
                    continue;
                }

                ACIA_Cxt.term_fd = newsock;
            }
        }

        if(ACIA_Cxt.term_fd >= 0)
        {
            fds[0].fd = ACIA_Cxt.term_fd;
            fds[0].events = POLLIN | POLLHUP | POLLERR;

            while(ACIA_Cxt.term_fd >= 0)
            {
                r = poll(fds, 2, -1);

                if(fds[1].revents == POLLIN)
                {
                    shutdown = true;
                    continue;
                }
                else if(fds[0].revents != 0)
                {
                    if(fds[0].revents == POLLIN)
                    {
                        /* Input data available is the only event, so process it. */
                        r = read(ACIA_Cxt.term_fd, buf, 256);
                    }
                    else
                    {
                        /* Some other condition occurred (hangup, error), so flag r < 0
                         * to close the socket. */
                        r = -1;
                    }

                    if(r <= 0)
                    {
                        pthread_mutex_lock(&ACIA_Cxt.mutex);

                        /* Close and invalidate the socket handle atomically, so a potential
                         * write call does not attempt to write to the socket after we close it. */
                        close(ACIA_Cxt.term_fd);
                        ACIA_Cxt.term_fd = -1;

                        pthread_mutex_unlock(&ACIA_Cxt.mutex);
                    }
                }
            }

        }
    }

    return NULL;
}

bool acia_init(char *socketpath)
{
    int fd;
    struct sockaddr_un sockname;

    if(pthread_mutex_init(&ACIA_Cxt.mutex, NULL) < 0)
    {
        fprintf(stderr, "UNable to init mutex\n");
        return false;
    }

    fd = socket(AF_UNIX, SOCK_STREAM, 0);

    if(fd < 0)
    {
        fprintf(stderr, "Unable to create server socket: %s\n", strerror(errno));
        return false;
    }

    sockname.sun_family = AF_UNIX;
    strncpy(sockname.sun_path, socketpath, sizeof(sockname.sun_path));
    unlink(sockname.sun_path);

    if(bind(fd, (struct sockaddr *)&sockname, strlen(sockname.sun_path) + sizeof(sockname.sun_family)) < 0)
    {
        fprintf(stderr, "Unable to bind to server socket: %s\n", strerror(errno));
        close(fd);
        return false;
    }

    if(listen(fd, 1) < 0)
    {
        fprintf(stderr, "Unable to listen on server socket: %s\n", strerror(errno));
        close(fd);
        return false;
    }

    ACIA_Cxt.server_fd = fd;
    ACIA_Cxt.shutdown_fd = eventfd(0, 0);
    ACIA_Cxt.term_fd = -1;

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

    printf("ACIA listening on: %s\n", socketpath);

    ACIA_Cxt.init = true;
    return true;
}

void acia_write(uint8_t reg, uint8_t val)
{
    if(reg == 0)
    {
        pthread_mutex_lock(&ACIA_Cxt.mutex);

        /* Under lock, so FD cannot be closed by read thread. FD should also be non-blocking. */
        if(ACIA_Cxt.term_fd >= 0)
            write(ACIA_Cxt.term_fd, &val, 1);

        pthread_mutex_unlock(&ACIA_Cxt.mutex);
    }
}

uint8_t acia_read(uint8_t reg)
{
    return 0;
}

void acia_cleanup(void)
{
    uint64_t eventval;

    if(ACIA_Cxt.init)
    {
        eventval = 1;

        write(ACIA_Cxt.shutdown_fd, &eventval, 8);

        pthread_join(ACIA_Cxt.thread, NULL);

        close(ACIA_Cxt.server_fd);
        close(ACIA_Cxt.shutdown_fd);

        if(ACIA_Cxt.term_fd > 0)
            close(ACIA_Cxt.term_fd);

        pthread_mutex_destroy(&ACIA_Cxt.mutex);
    }
}
