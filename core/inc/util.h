#ifndef __UTIL_H__
#define __UTIL_H__

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/**
 * List node structure to be used as a list head or as a member
 * in a structure that needs to be tracked in a list. It can be anywhere
 * inside the structure and allows any kind of data structure to also be
 * tracked by any number of lists by using multiple node instances.
 */
typedef struct listnode_s
{
    struct listnode_s *next; /**< Next entry in the circular linked list. */
    struct listnode_s *prev; /**< Previous entry in the circular linked list. */
} listnode_t;

/**
 * Initializer to create an empty list head. All lists contain a dummy head node so that
 * the next and prev pointer are never NULL. A list head should be a listnode_t structure declared
 * outside of the normal list entry structure and initialized using this macro.
 */
#define LIST_HEAD_INIT(_head)  { &(_head), &(_head) }

/**
 * Determines a pointer to the container type of supplied list node pointer.
 *
 * @param[in] _node Pointer to the listnode_t for a given list entry
 * @param[in] _type The structure data type of the list entry
 * @param[in] _member The name of the node field within the data structure
 */
#define list_container(_node, _type, _member) (_type *)(((uint8_t *)(_node)) - offsetof(_type, _member))

/**
 * Determines if a given list is empty based on the supplied head node.
 *
 * @param[in] head List head to check for emptiness.
 *
 * @return true if the list is empty or false otherwise
 */
static inline bool list_empty(listnode_t *head)
{
    /* A list is considered empty if the only node is the dummy head. Thus, the head
     * points to itself. */
    return head->next == head;
}

/**
 * Add the supplied node to the tail of the given list
 *
 * @param[in] head The head of the list to add to
 * @param[in] add The entry to add
 */
static inline void list_add_tail(listnode_t *head, listnode_t *add)
{
    head->prev->next = add;
    add->prev = head->prev;
    head->prev = add;
    add->next = head;
}

/**
 * Add the supplied node to the head of the given list
 *
 * @param[in] head The head of the list to add to
 * @param[in] add The entry to add
 */
static inline void list_add_head(listnode_t *head, listnode_t *add)
{
    head->next->prev = add;
    add->next = head->next;
    head->next = add;
    add->prev = head;
}

/**
 * Remove the given node from its list
 *
 * @param[in] rem The node to remove
 */
static inline void list_remove(listnode_t *rem)
{
    rem->prev->next = rem->next;
    rem->next->prev = rem->prev;
}

/**
 * Macro to interate through a given list.
 */
#define list_iterate(_head, _nodeptr) for(_nodeptr = (_head)->next; _nodeptr != (_head); _nodeptr = _nodeptr->next)

/**
 * Determine if the given list contains the given node
 *
 * @param[in] head The head if the list to check
 * @param[in] node The node to check the list for
 *
 * @return true if the list contains the given node
 */
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
