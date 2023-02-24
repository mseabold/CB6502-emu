#ifndef __UTIL_H__
#define __UTIL_H__

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef struct listnode_s
{
    struct listnode_s *next;
    struct listnode_s *prev;
} listnode_t;

#define LIST_HEAD_INIT(_head)  { &(_head), &(_head) }
#define list_container(_node, _type, _member) (_type *)(((uint8_t *)(_node)) - offsetof(_type, _member))

static inline bool list_empty(listnode_t *head)
{
    return head->next == head;
}

static inline void list_add_tail(listnode_t *head, listnode_t *add)
{
    head->prev->next = add;
    add->prev = head->prev;
    head->prev = add;
    add->next = head;
}
static inline void list_add_head(listnode_t *head, listnode_t *add)
{
    head->next->prev = add;
    add->next = head->next;
    head->next = add;
    add->prev = head;
}

static inline void list_remove(listnode_t *rem)
{
    rem->prev->next = rem->next;
    rem->next->prev = rem->prev;
}

#define list_iterate(_head, _nodeptr) for(_nodeptr = (_head)->next; _nodeptr != (_head); _nodeptr = _nodeptr->next)

static inline bool list_contains(listnode_t *head, listnode_t *node)
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



#endif /* end of include guard: __UTIL_H__ */
