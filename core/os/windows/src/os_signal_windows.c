#include <windows.h>
#include <stdlib.h>

#include "util.h"
#include "os_signal.h"

typedef struct
{
    os_signal_t signal;
    os_sigcb_t callback;
    void *userdata;
    listnode_t node;
} signal_data_t;

static listnode_t ctlc_list = LIST_HEAD_INIT(ctlc_list);
static unsigned int num_registered;

static BOOL WINAPI CtrlHandler(DWORD fdwCtrlType)
{
    signal_data_t *data;
    listnode_t *head;
    listnode_t *iter;

    switch(fdwCtrlType)
    {
        case CTRL_C_EVENT:
            head = &ctlc_list;
            break;
        default:
            return FALSE;
    }

    list_iterate(head, iter)
    {
        data = list_container(iter, signal_data_t, node);

        data->callback(data->signal, data->userdata);
    }

    return TRUE;
}

os_sigcb_handle_t os_register_signal(os_signal_t signal, os_sigcb_t cb, void *userdata)
{
    signal_data_t *data;
    listnode_t *head;

    if(cb == NULL)
    {
        return NULL;
    }

    switch(signal)
    {
        case OS_CTRLC:
            head = &ctlc_list;
            break;
        default:
            return NULL;
    }

    data = malloc(sizeof(signal_data_t));

    if(data == NULL)
    {
        return NULL;
    }

    memset(data, 0, sizeof(signal_data_t));

    data->callback = cb;
    data->signal = signal;
    data->userdata = userdata;

    if(num_registered == 0)
    {
        SetConsoleCtrlHandler(CtrlHandler, TRUE);
    }

    ++num_registered;

    list_add_tail(head, &data->node);

    return data;
}

void os_unregister_signal(os_sigcb_handle_t handle)
{
    signal_data_t *data = (signal_data_t *)handle;
    listnode_t *head;

    if(data == NULL)
    {
        return;
    }

    switch(data->signal)
    {
        case OS_CTRLC:
            head = &ctlc_list;
            break;
        default:
            return;
    }

    if(!list_contains(head, &data->node))
    {
        return;
    }

    list_remove(&data->node);

    --num_registered;

    if(num_registered == 0)
    {
        SetConsoleCtrlHandler(CtrlHandler, FALSE);
    }

    free(data);
}