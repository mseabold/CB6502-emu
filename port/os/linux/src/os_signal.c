#include <signal.h>
#include <stdlib.h>
#include <string.h>

#include "os_signal.h"
#include "util.h"

typedef struct
{
    os_signal_t signal;
    os_sigcb_t callback;
    void *userdata;
    listnode_t node;
} signal_data_t;

static struct sigaction old_sigint;

static listnode_t sigint_list = LIST_HEAD_INIT(sigint_list);

void sig_handler(int signum)
{
    listnode_t *iter;
    listnode_t *head;
    signal_data_t *data;

    switch(signum)
    {
        case SIGINT:
            head = &sigint_list;
            break;
        default:
            return;
    }

    list_iterate(head, iter)
    {
        data = list_container(iter, signal_data_t, node);

        data->callback(data->signal, data->userdata);
    }
}

os_sigcb_handle_t os_register_signal(os_signal_t signal, os_sigcb_t cb, void *userdata)
{
    signal_data_t *data;
    listnode_t *listhead;
    struct sigaction *old_action;
    struct sigaction new_action;
    int signum;

    if(cb == NULL)
    {
        return NULL;
    }

    data = malloc(sizeof(signal_data_t));

    if(data == NULL)
    {
        return NULL;
    }

    memset(data, 0, sizeof(signal_data_t));

    data->signal = signal;
    data->callback = cb;
    data->userdata = userdata;

    switch(signal)
    {
        case OS_CTRLC:
            listhead = &sigint_list;
            break;
        default:
            free(data);
            return NULL;
    }

    if(list_empty(listhead))
    {
        /* List is currently empty. We haven't registered the signal yet so we need to do so now. */
        switch(signal)
        {
            case OS_CTRLC:
                old_action = &old_sigint;
                signum = SIGINT;
                break;
        }

        new_action.sa_handler = sig_handler;
        new_action.sa_flags = 0;
        sigemptyset(&new_action.sa_mask);

        if(sigaction(signum, &new_action, old_action) < 0)
        {
            free(data);
            return NULL;
        }
    }

    list_add_tail(listhead, &data->node);

    return data;
}

void os_unregister_signal(os_sigcb_handle_t handle)
{
    int signum;
    signal_data_t *data = (signal_data_t *)handle;
    struct sigaction *old_action;
    listnode_t *listhead;

    if(handle == NULL)
    {
        return;
    }

    switch(data->signal)
    {
        case OS_CTRLC:
            listhead = &sigint_list;
            break;
        default:
            return;
    }

    /* Check the the list actually contains this node. If not, there is
     * some kind of error, so do nothing. */
    if(!list_contains(listhead, &data->node))
    {
        return;
    }

    list_remove(&data->node);

    if(list_empty(listhead))
    {
        /* List handler for this signal was un-registered, so we can also
         * restore the original action. */
        switch(data->signal)
        {
            case OS_CTRLC:
                signum = data->signal;
                old_action = &old_sigint;
                break;
        }

        sigaction(signum, old_action, NULL);
    }

    free(data);
}
