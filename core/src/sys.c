#include "sys.h"
#include <stdlib.h>
#include <string.h>

typedef struct mem_trace_entry_s
{
    mem_trace_t cb;
    void *param;
    struct mem_trace_entry_s *next;
} mem_trace_entry_t;

struct sys_cxt_s
{
    mem_space_t mem_space;
    uint32_t nIrqVotes;
    uint32_t nNmiVotes;
    bool nmiPend;
    uint32_t tickrate;

    mem_trace_entry_t dummy_head;

    mem_trace_entry_t *mem_trace_head;
    mem_trace_entry_t *mem_trace_tail;
};

#define NUM_BALLOTS 32

sys_cxt_t sys_init(const mem_space_t *mem_space)
{
    sys_cxt_t cxt;

    if(mem_space == NULL || mem_space->write == NULL || mem_space->read == NULL || mem_space->peek == NULL)
        return NULL;

    cxt = malloc(sizeof(struct sys_cxt_s));

    if(cxt == NULL)
        return NULL;

    memset(cxt, 0, sizeof(struct sys_cxt_s));
    cxt->mem_space = *mem_space;
    cxt->tickrate = DEFAULT_TICKRATE_NS;
    cxt->mem_trace_head = cxt->mem_trace_tail = &cxt->dummy_head;

    return (sys_cxt_t)cxt;
}

void sys_destroy(sys_cxt_t cxt)
{
    if(cxt != NULL)
        free(cxt);
}

void sys_vote_interrupt(sys_cxt_t cxt, bool nmi, bool vote)
{
    uint32_t *voteCnt;

    if(cxt == NULL)
        return;

    voteCnt = nmi ? &(cxt->nNmiVotes) : &(cxt->nIrqVotes);

    if(vote && *voteCnt < UINT32_MAX)
    {
        ++(*voteCnt);

        if(nmi && *voteCnt == 1)
        {
            /* NMI is edge triggered, so on first vote set it pending. It will only be cleared once processed. */
            cxt->nmiPend = true;
        }
    }
    else if(!vote && *voteCnt > 0)
        --(*voteCnt);
}

bool sys_check_interrupt(sys_cxt_t cxt, bool nmi)
{
    if(cxt == NULL)
        return false;

    if(nmi && cxt->nmiPend)
    {
        /* Reset the edge triggered NMI. */
        cxt->nmiPend = false;
        return true;
    }

    if(!nmi && cxt->nIrqVotes > 0)
    {
        /* IRQ is level triggered, so it is pending so long as there are votes. */
        return true;
    }

    return false;
}

uint8_t sys_read_mem(sys_cxt_t cxt, uint16_t addr)
{
    uint8_t val;
    mem_trace_entry_t *trace;

    if(cxt == NULL)
        return 0xff;

    val = cxt->mem_space.read(addr);

    trace = cxt->mem_trace_head->next;

    while(trace != NULL)
    {
        trace->cb(addr, val, false, trace->param);
        trace = trace->next;
    }

    return val;
}

void sys_write_mem(sys_cxt_t cxt, uint16_t addr, uint8_t val)
{
    mem_trace_entry_t *trace;

    if(cxt == NULL)
        return;

    trace = cxt->mem_trace_head->next;

    while(trace != NULL)
    {
        trace->cb(addr, val, true, trace->param);
        trace = trace->next;
    }

    cxt->mem_space.write(addr, val);
}

uint8_t sys_peek_mem(sys_cxt_t cxt, uint16_t addr)
{
    if(cxt == NULL)
        return 0xff;

    return cxt->mem_space.peek(addr);
}

void sys_set_tickrate(sys_cxt_t cxt, uint32_t tickrate)
{
    if(cxt == NULL)
        return;

    cxt->tickrate = tickrate;
}

uint64_t sys_convert_ticks_to_ns(sys_cxt_t cxt, uint32_t ticks)
{
    if(cxt == NULL)
        return 0;

    return (uint64_t)ticks * (uint64_t)cxt->tickrate;
}

sys_trace_cb_t sys_register_mem_trace_callback(sys_cxt_t cxt, mem_trace_t cb, void *param)
{
    mem_trace_entry_t *new;

    if(cxt == NULL)
        return false;

    new = malloc(sizeof(mem_trace_entry_t));

    if(new == NULL)
        return NULL;

    memset(new, 0, sizeof(mem_trace_entry_t));
    new->cb = cb;
    new->param = param;

    cxt->mem_trace_tail->next = new;
    cxt->mem_trace_tail = new;

    return new;
}

void sys_un_register_mem_trace_callback(sys_cxt_t cxt, sys_trace_cb_t cb)
{
    mem_trace_entry_t *cur;
    mem_trace_entry_t *prev;

    if(cxt == NULL || cb == NULL)
        return;

    cur = cxt->mem_trace_head;
    prev = NULL;

    while(cur != NULL && cb != cur)
    {
        prev = cur;
        cur = cur->next;
    }

    if(cur == NULL)
        return;

    prev->next = cur->next;

    if(cxt->mem_trace_tail == cur)
        cxt->mem_trace_tail = prev;

    free(cur);
}
