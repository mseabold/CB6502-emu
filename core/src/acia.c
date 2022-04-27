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

#define ACIA_RS_TX_DATA 0x00
#define ACIA_RS_RX_DATA 0x00
#define ACIA_RS_SW_RESET 0x01
#define ACIA_RS_STATUS   0x01
#define ACIA_RS_CMD      0x02
#define ACIA_RS_CTRL     0x03

#define ACIA_STATUS_IRQ   0x80
#define ACIA_STATUS_DSRB  0x40
#define ACIA_STATUS_DCDB  0x20
#define ACIA_STATUS_TDRE  0x10
#define ACIA_STATUS_RDRF  0x08
#define ACIA_STATUS_OVER  0x04
#define ACIA_STATUS_FRAME 0x02
#define ACIA_STATUS_PAR   0x01

#define ACIA_RX_READ_CLEAR_BITS (ACIA_STATUS_RDRF | ACIA_STATUS_OVER | ACIA_STATUS_FRAME | ACIA_STATUS_PAR)

#define ACIA_CTRL_SBN_MASK 0x80
#define ACIA_CTRL_SBN_1_BIT 0x00
#define ACIA_CTRL_SBN_2_BIT 0x80

#define ACIA_CTRL_WL_MASK 0x60
#define ACIA_CTRL_WL_8_BITS 0x00
#define ACIA_CTRL_WL_7_BITS 0x20
#define ACIA_CTRL_WL_6_BITS 0x40
#define ACIA_CTRL_WL_5_BITS 0x60

#define ACIA_CTRL_RCS_MASK 0x10
#define ACIA_CTRL_RCS_EXT  0x00
#define ACIA_CTRL_RCS_BAUD 0x10

#define ACIA_CTRL_SBR_MASK 0x0f
/* Unused currently, so skip defining. */

#define ACIA_CTRL_HW_RESET_VAL 0x00

#define ACIA_CMD_PMC_MASK 0xc0
#define ACIA_CMD_PME_MASK 0x20
#define ACIA_CMD_REM_MASK 0x10
#define ACIA_CMD_TIC_MASK 0x0c
#define ACIA_CMD_IRD_MASK 0x02
#define ACIA_CMD_DTR_MASK 0x01

#define ACIA_CMD_IRD_ENABLED  0x00
#define ACIA_CMD_IRD_DISABLED 0x02

#define ACIA_CMD_HW_RESET_VAL 0x00
#define ACIA_CMD_SW_RESET_MASK 0x1f
#define ACIA_CMD_SW_RESET_VAL  0x00

#define ACIA_RX_BUF_SIZE 256

typedef struct ACIA_Cxt_s
{
    int server_fd;
    int term_fd;
    int shutdown_fd;
    pthread_t thread;
    pthread_mutex_t mutex;

    uint8_t ctl_reg;
    uint8_t cmd_reg;
    uint8_t stat_reg;

    uint8_t rx_buffer[ACIA_RX_BUF_SIZE];
    uint32_t rx_buf_in;
    uint32_t rx_buf_out;
    uint32_t rx_avail;

    bool irq_pend;
} ACIA_Cxt_t;

static void check_rx_rdy(ACIA_Cxt_t *cxt)
{
    if(cxt->rx_avail < ACIA_RX_BUF_SIZE)
    {
        cxt->stat_reg |= ACIA_STATUS_RDRF;
    }
}

static void eval_irq(ACIA_Cxt_t *cxt)
{
    bool irq = false;

    if((cxt->stat_reg & ACIA_STATUS_RDRF) && ((cxt->cmd_reg & ACIA_CMD_IRD_MASK) == ACIA_CMD_IRD_ENABLED))
        irq = true;

    if(irq)
        cxt->stat_reg |= ACIA_STATUS_IRQ;
    else
        cxt->stat_reg &= ~ACIA_STATUS_IRQ;
}

void *terminal_thread(void *p)
{
    struct pollfd fds[2];
    bool shutdown = false;
    uint8_t buf[256];
    int newsock;
    ACIA_Cxt_t *cxt = (ACIA_Cxt_t *)p;

    while(!shutdown)
    {
        int r;

        fds[0].events = POLLIN;
        fds[0].fd = cxt->server_fd;
        fds[1].events = POLLIN;
        fds[1].fd = cxt->shutdown_fd;

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
                newsock = accept4(cxt->server_fd, NULL, NULL, SOCK_NONBLOCK);

                if(newsock < 0)
                {
                    continue;
                }

                cxt->term_fd = newsock;
            }
        }

        if(cxt->term_fd >= 0)
        {
            fds[0].fd = cxt->term_fd;
            fds[0].events = POLLIN | POLLHUP | POLLERR;

            while(cxt->term_fd >= 0)
            {
                r = poll(fds, 2, -1);

                if(fds[1].revents == POLLIN)
                {
                    shutdown = true;
                    break;
                }
                else if(fds[0].revents != 0)
                {
                    if(fds[0].revents == POLLIN)
                    {
                        /* Input data available is the only event, so process it. */
                        r = read(cxt->term_fd, buf, 256);
                    }
                    else
                    {
                        /* Some other condition occurred (hangup, error), so flag r < 0
                         * to close the socket. */
                        r = -1;
                    }

                    if(r > 0)
                    {
                        int copylen;
                        bool irq_pend;

                        pthread_mutex_lock(&cxt->mutex);

                        if(r >= cxt->rx_avail)
                        {
                            fprintf(stderr, "Warning: Dropping %d bytes in ACIA\n", r - (int)cxt->rx_avail);
                            r = (int)cxt->rx_avail;
                        }

                        if(cxt->rx_buf_in + r >= ACIA_RX_BUF_SIZE)
                        {
                            copylen = ACIA_RX_BUF_SIZE - ACIA_RX_BUF_SIZE;
                            memcpy(&cxt->rx_buffer[cxt->rx_buf_in], buf, copylen);
                            cxt->rx_buf_in = 0;
                            r -= copylen;
                            cxt->rx_avail -= copylen;
                        }

                        if(r > 0)
                        {
                            memcpy(&cxt->rx_buffer[cxt->rx_buf_in], buf, r);
                            cxt->rx_avail -= r;
                            cxt->rx_buf_in += r;
                        }

                        check_rx_rdy(cxt);

                        pthread_mutex_unlock(&cxt->mutex);

                        eval_irq(cxt);
                    }
                    else
                    {
                        pthread_mutex_lock(&cxt->mutex);

                        /* Close and invalidate the socket handle atomically, so a potential
                         * write call does not attempt to write to the socket after we close it. */
                        close(cxt->term_fd);
                        cxt->term_fd = -1;

                        pthread_mutex_unlock(&cxt->mutex);
                    }
                }
            }

        }
    }

    return NULL;
}

acia_t acia_init(char *socketpath)
{
    int fd;
    struct sockaddr_un sockname;
    ACIA_Cxt_t *cxt = malloc(sizeof(ACIA_Cxt_t));

    if(cxt == NULL)
        return NULL;

    memset(cxt, 0, sizeof(ACIA_Cxt_t));

    if(pthread_mutex_init(&cxt->mutex, NULL) < 0)
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

    cxt->server_fd = fd;
    cxt->shutdown_fd = eventfd(0, 0);
    cxt->term_fd = -1;

    if(cxt->shutdown_fd < 0)
    {
        fprintf(stderr, "eventfd failure %s\n", strerror(errno));
        close(fd);
        return false;
    }

    if(pthread_create(&cxt->thread, NULL, terminal_thread, cxt) < 0)
    {
        fprintf(stderr, "Unable to start ACIA thread: %s\n", strerror(errno));
        close(cxt->term_fd);
        close(cxt->shutdown_fd);
        return false;
    }

    printf("ACIA listening on: %s\n", socketpath);

    cxt->rx_avail = ACIA_RX_BUF_SIZE;

    return (acia_t)cxt;
}

void acia_write(acia_t handle, uint8_t reg, uint8_t val)
{
    ACIA_Cxt_t *cxt = (ACIA_Cxt_t *)handle;

    if(cxt == NULL)
        return;

    switch(reg)
    {
        case ACIA_RS_TX_DATA:
            pthread_mutex_lock(&cxt->mutex);

            /* Under lock, so FD cannot be closed by read thread. FD should also be non-blocking. */
            if(cxt->term_fd >= 0)
                write(cxt->term_fd, &val, 1);

            pthread_mutex_unlock(&cxt->mutex);
            break;
        case ACIA_RS_SW_RESET:
            /* TODO */
            break;
        case ACIA_RS_CTRL:
            cxt->ctl_reg = val;
            break;
        case ACIA_RS_CMD:
            cxt->cmd_reg = val;
            break;
    }
}

uint8_t acia_read(acia_t handle, uint8_t reg)
{
    uint8_t ret = 0xff;
    ACIA_Cxt_t *cxt = (ACIA_Cxt_t *)handle;

    if(cxt == NULL)
        return 0xff;

    switch(reg)
    {
        case ACIA_RS_RX_DATA:
            pthread_mutex_lock(&cxt->mutex);

            if(cxt->stat_reg & ACIA_STATUS_RDRF)
            {
                /* TODO: what does the actual ACIA do if there is no data available? For now,
                 *       just return whatever was last received. */
                ret = cxt->rx_buffer[cxt->rx_buf_out];
                cxt->rx_buf_out = (cxt->rx_buf_out + 1) % ACIA_RX_BUF_SIZE;
                ++cxt->rx_avail;
                cxt->stat_reg &= ~ACIA_RX_READ_CLEAR_BITS;

                (void)check_rx_rdy(cxt);
            }

            pthread_mutex_unlock(&cxt->mutex);

            eval_irq(cxt);
            break;
        case ACIA_RS_STATUS:
            pthread_mutex_lock(&cxt->mutex);
            ret = cxt->stat_reg;
            cxt->stat_reg &= ~ACIA_STATUS_IRQ;
            pthread_mutex_unlock(&cxt->mutex);
            break;
        case ACIA_RS_CTRL:
            ret = cxt->ctl_reg;
            break;
        case ACIA_RS_CMD:
            ret = cxt->cmd_reg;
            break;
    }

    return ret;
}

void acia_cleanup(acia_t handle)
{
    ACIA_Cxt_t *cxt = (ACIA_Cxt_t *)handle;
    uint64_t eventval;

    if(cxt == NULL)
        return;

    eventval = 1;

    write(cxt->shutdown_fd, &eventval, 8);

    pthread_join(cxt->thread, NULL);

    close(cxt->server_fd);
    close(cxt->shutdown_fd);

    if(cxt->term_fd > 0)
        close(cxt->term_fd);

    pthread_mutex_destroy(&cxt->mutex);

    free(cxt);
}
