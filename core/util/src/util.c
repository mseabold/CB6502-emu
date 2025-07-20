#include <stdlib.h>
#include "util.h"

void list_free_offset_cb(listnode_t *node, void *param)
{
    uintptr_t offset = (uintptr_t)param;
    uint8_t *container = (uint8_t *)node - offset;

    free(container);
}

bool list_contains(listnode_t *head, listnode_t *node)
{
    listnode_t *iter;
    list_iterate(head, iter)
    {
        if(iter == node)
        {
            return true;
        }
    }

    return false;
}

void list_free(listnode_t *head, list_free_entry_cb_t callback, void *param)
{
    listnode_t *tail;

    while(!list_empty(head))
    {
        tail = list_tail(head);
        list_remove(tail);

        if(callback != NULL)
        {
            callback(tail, param);
        }
    }
}
