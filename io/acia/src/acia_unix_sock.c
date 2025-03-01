#define _GNU_SOURCE
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <sys/poll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include "acia.h"
#include "acia_unix_sock.h"
#include "log.h"
#include "sys.h"

#define RING_BUFFER_SIZE 128

struct acia_unix_s
{
    unsigned int read_idx;
    unsigned int write_idx;
    unsigned int num_bytes;
    uint8_t buffer[RING_BUFFER_SIZE];

    const char *sockname;
    int server_sock;
    int client_sock;
    int eventfd;
    bool shutdown;

    pthread_t thread_handle;
    pthread_mutex_t lock;
    pthread_cond_t cond;
};

typedef struct acia_unix_s *acia_unix_t;

static void process_client(acia_unix_t cxt)
{
    int result;
    bool done;

    struct pollfd fds[2];

    fds[0].fd = cxt->client_sock;
    fds[0].events = POLLIN | POLLHUP | POLLERR;
    fds[1].fd = cxt->eventfd;
    fds[1].events = POLLIN;

    done = false;

    while(!done)
    {
        result = poll(fds, 2, -1);

        if(result > 0)
        {
            if(fds[1].revents == POLLIN)
            {
                /* Shutdown was signalled, so exit the client loop and signal the main
                 * thread loop to shutdown as well. */
                done = true;
            }
            else
            {
                if(fds[0].revents == POLLIN)
                {
                    unsigned int avail;

                    /* Wait until there is at least some space available in the ring buffer. */
                    pthread_mutex_lock(&cxt->lock);

                    avail = RING_BUFFER_SIZE - cxt->num_bytes;

                    while(avail == 0 && !cxt->shutdown)
                    {
                        pthread_cond_wait(&cxt->cond, &cxt->lock);
                        avail = RING_BUFFER_SIZE - cxt->num_bytes;
                    }

                    pthread_mutex_unlock(&cxt->lock);

                    if(cxt->shutdown)
                    {
                        done = true;
                        continue;
                    }

                    /* We have some bytes available. It's safe to use this count unlocked now,
                     * since the reader can only make it larger. If the available space wraps
                     * around the buffer, we'll just read to the end of the buffer for now. */
                    if(cxt->write_idx + avail >= RING_BUFFER_SIZE)
                        avail = RING_BUFFER_SIZE - cxt->write_idx;

                    result = read(cxt->client_sock, &cxt->buffer[cxt->write_idx], avail);

                    if(result > 0)
                    {
                        cxt->write_idx += result;

                        if(cxt->write_idx >= RING_BUFFER_SIZE)
                            cxt->write_idx -= RING_BUFFER_SIZE;

                        /* Lock the context to change the number of bytes. */
                        pthread_mutex_lock(&cxt->lock);

                        cxt->num_bytes += result;

                        pthread_mutex_unlock(&cxt->lock);
                    }
                    else
                    {
                        /* read() returned 0/error, which indicates that the socket has been
                         * closed remotely most likely. */
                        done = true;
                    }
                }
                else if(fds[0].revents != 0)
                {
                    /* Error or hangup. */
                    done = true;
                }
            }
        }
        else if(result < 0)
        {
            log_print(lNOTICE, "poll() returned error: %s\n", strerror(errno));
        }
    }

    if(!cxt->shutdown)
    {
        log_print(lDEBUG, "Unix Sock client closed.\n");
    }
}

static void *read_thread(void *params)
{
    int result;
    bool done;
    struct pollfd fds[2];

    acia_unix_t cxt = (acia_unix_t)params;

    if(params == NULL)
        return NULL;

    done = false;

    while(!done && !cxt->shutdown)
    {
        fds[0].fd = cxt->server_sock;
        fds[0].events = POLLIN;

        fds[1].fd = cxt->eventfd;
        fds[1].events = POLLIN;

        result = poll(fds, 2, -1);

        if(result > 0)
        {
            if(fds[1].revents == POLLIN)
            {
                /* Shutdown signalled. */
                done = true;
            }
            else if(fds[0].revents == POLLIN)
            {
                cxt->client_sock = accept4(cxt->server_sock, NULL, NULL, SOCK_NONBLOCK);

                if(cxt->client_sock >= 0)
                {
                    /* Handle the new client until it is closed or a shutdown signal
                     * is received. Returns true if a shutdown was received. */
                    process_client(cxt);

                    /* Close the client socket since we are done. Do this atomically with
                     * invalidating the handle so that another thread won't try to write to
                     * it while we're invalidating. */
                    pthread_mutex_lock(&cxt->lock);
                    close(cxt->client_sock);
                    cxt->client_sock = -1;

                    pthread_mutex_unlock(&cxt->lock);
                }
            }
        }
        else if(result < 0)
        {
            log_print(lNOTICE, "poll() returned error: %s\n", strerror(errno));
        }
    }

    return NULL;
}

/* Note, this assume the read thread has been stopped and it is safe to close
 * the locks/conditions. */
static void destroy_context(acia_unix_t cxt)
{
    pthread_mutex_destroy(&cxt->lock);
    pthread_cond_destroy(&cxt->cond);

    if(cxt->server_sock >= 0)
        close(cxt->server_sock);

    if(cxt->eventfd >= 0)
        close(cxt->eventfd);

    unlink(cxt->sockname);
    free(cxt);
}

static void *acia_unix_init(void *params)
{
    acia_unix_t cxt;
    acia_unix_sock_params_t *unix_p = (acia_unix_sock_params_t *)params;
    struct sockaddr_un sockname;
    int fd;

    if(unix_p == NULL)
        return NULL;

    cxt = malloc(sizeof(struct acia_unix_s));

    if(cxt == NULL)
    {
        log_print(lNOTICE, "Unable to allocate context\n");
        return NULL;
    }

    memset(cxt, 0, sizeof(struct acia_unix_s));
    cxt->server_sock = -1;
    cxt->client_sock = -1;
    cxt->eventfd = -1;
    cxt->sockname = unix_p->sockname;

    if(pthread_mutex_init(&cxt->lock, NULL) < 0)
    {
        log_print(lNOTICE, "Unable to create mutex\n");
        free(cxt);
        return NULL;
    }

    if(pthread_cond_init(&cxt->cond, NULL) < 0)
    {
        log_print(lNOTICE, "Unable to create pthread condition \n");
        pthread_mutex_destroy(&cxt->lock);
        free(cxt);
        return NULL;
    }

    fd = eventfd(0, 0);

    if(fd < 0)
    {
        log_print(lNOTICE, "Unable to create eventfd: %s\n", strerror(errno));
        goto error;
    }

    cxt->eventfd = fd;

    fd = socket(AF_UNIX, SOCK_STREAM, 0);

    if(fd < 0)
    {
        log_print(lNOTICE, "Unable to create unix server socket: %s\n", strerror(errno));
        goto error;
    }

    cxt->server_sock = fd;

    sockname.sun_family = AF_UNIX;
    strncpy(sockname.sun_path, cxt->sockname, sizeof(sockname.sun_path));
    sockname.sun_path[sizeof(sockname.sun_path)-1] = '\0';
    unlink(sockname.sun_path);

    if(bind(fd, (struct sockaddr *)&sockname, strlen(sockname.sun_path) + sizeof(sockname.sun_family)) < 0)
    {
        log_print(lNOTICE, "Unable to bind server socket to %s. (%s)\n", sockname.sun_path, strerror(errno));
        goto error;
    }

    if(listen(fd, 1) < 0)
    {
        log_print(lNOTICE, "listen() failure: %s\n", strerror(errno));
        goto error;
    }

    if(pthread_create(&cxt->thread_handle, NULL, read_thread, cxt) < 0)
    {
        log_print(lNOTICE, "Unable to start thread: %s\n", strerror(errno));
        goto error;
    }

    /* Successfully started. */
    return cxt;

error:
    destroy_context(cxt);
    return NULL;
}

static bool acia_unix_available(void *handle)
{
    acia_unix_t cxt = (acia_unix_t)handle;

    if(cxt == NULL)
        return false;

    return cxt->num_bytes > 0;
}

/* Note that this will never block, even if there is no data available. If nothing
 * is available, it will just return ff. */
static uint8_t acia_unix_read(void *handle)
{
    acia_unix_t cxt = (acia_unix_t)handle;
    uint8_t data;

    if(cxt == NULL)
        return 0;

    if(cxt->num_bytes == 0)
        return 0xff;

    data = cxt->buffer[cxt->read_idx++];

    if(cxt->read_idx == RING_BUFFER_SIZE)
        cxt->read_idx = 0;

    /* Lock the context to update the number of bytes available as well as signal
     * the read thread it case it was blocked on a full buffer. */
    pthread_mutex_lock(&cxt->lock);

    --cxt->num_bytes;
    pthread_cond_signal(&cxt->cond);

    pthread_mutex_unlock(&cxt->lock);

    return data;
}

static void acia_unix_write(void *handle, uint8_t data)
{
    acia_unix_t cxt = (acia_unix_t)handle;

    if(cxt == NULL)
        return;

    /* Do the write under lock, so that the socket is not closed while we use it.
     * The client is accepted as non-blocking, so we don't have to worry about getting
     * stuck under lock. */
    pthread_mutex_lock(&cxt->lock);

    if(cxt->client_sock >= 0)
    {
        /* We consider this transport "unreliable", so don't bother to check the result
         * if write() */
        (void)send(cxt->client_sock, &data, 1, MSG_NOSIGNAL);
    }

    pthread_mutex_unlock(&cxt->lock);
}

static void acia_unix_cleanup(void *handle)
{
    acia_unix_t cxt = (acia_unix_t)handle;

    if(cxt == NULL)
        return;

    pthread_mutex_lock(&cxt->lock);

    cxt->shutdown = true;
    pthread_cond_signal(&cxt->cond);

    /* Signal the thread to shutdown. */
    eventfd_write(cxt->eventfd, 1);

    pthread_mutex_unlock(&cxt->lock);

    /* Wait for the thread to finish. */
    pthread_join(cxt->thread_handle, NULL);

    destroy_context(cxt);
}

const acia_trans_interface_t acia_unix_iface =
{
    acia_unix_init,
    acia_unix_available,
    acia_unix_read,
    acia_unix_write,
    acia_unix_cleanup
};

const acia_trans_interface_t *acia_unix_get_iface(void)
{
    return &acia_unix_iface;
}
