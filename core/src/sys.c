#include "sys.h"
#include <stdlib.h>
#include <string.h>

typedef struct sys_cxt_internal_s
{
    mem_read_t read_hlr;
    mem_write_t write_hlr;
    uint32_t nIrqVotes;
    uint32_t nNmiVotes;
    bool nmiPend;
} sys_cxt_internal_t;

#define NUM_BALLOTS 32

sys_cxt_t sys_init(mem_read_t read_hlr, mem_write_t write_hlr)
{
    sys_cxt_internal_t *cxt;

    cxt = malloc(sizeof(sys_cxt_internal_t));

    if(cxt == NULL || read_hlr == NULL || write_hlr == NULL)
        return NULL;

    memset(cxt, 0, sizeof(sys_cxt_internal_t));
    cxt->read_hlr = read_hlr;
    cxt->write_hlr = write_hlr;

    return (sys_cxt_t)cxt;
}

void sys_destroy(sys_cxt_t cxt)
{
    if(cxt != NULL)
        free(cxt);
}

void sys_vote_interrupt(sys_cxt_t cxt, bool nmi, bool vote)
{
    sys_cxt_internal_t *cxt_i = (sys_cxt_internal_t *)cxt;
    uint32_t *voteCnt;

    if(cxt_i == NULL)
        return;

    voteCnt = nmi ? &(cxt_i->nNmiVotes) : &(cxt_i->nIrqVotes);

    if(vote && *voteCnt < UINT32_MAX)
    {
        ++(*voteCnt);

        if(nmi && *voteCnt == 1)
        {
            /* NMI is edge triggered, so on first vote set it pending. It will only be cleared once processed. */
            cxt_i->nmiPend = true;
        }
    }
    else if(!vote && *voteCnt > 0)
        --(*voteCnt);
}

bool sys_check_interrupt(sys_cxt_t cxt, bool nmi)
{
    sys_cxt_internal_t *cxt_i = (sys_cxt_internal_t *)cxt;

    if(cxt_i == NULL)
        return false;

    if(nmi && cxt_i->nmiPend)
    {
        /* Reset the edge triggered NMI. */
        cxt_i->nmiPend = false;
        return true;
    }

    if(!nmi && cxt_i->nIrqVotes > 0)
    {
        /* IRQ is level triggered, so it is pending so long as there are votes. */
        return true;
    }

    return false;
}

uint8_t sys_read_mem(sys_cxt_t cxt, uint16_t addr)
{
    sys_cxt_internal_t *cxt_i = (sys_cxt_internal_t *)cxt;

    if(cxt_i == NULL)
        return 0xff;

    return cxt_i->read_hlr(addr);
}

void sys_write_mem(sys_cxt_t cxt, uint16_t addr, uint8_t val)
{
    sys_cxt_internal_t *cxt_i = (sys_cxt_internal_t *)cxt;

    if(cxt_i == NULL)
        return;

    cxt_i->write_hlr(addr, val);
}
