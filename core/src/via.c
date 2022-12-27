#include "via.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define DATAB 0x00
#define DATAA 0x01
#define DDRB  0x02
#define DDRA  0x03
#define T1CL  0x04
#define T1CH  0x05
#define T1LL  0x06
#define T1LH  0x07
#define T2CL  0x08
#define T2CH  0x09
#define SR    0x0A
#define ACR   0x0B
#define PCR   0x0C
#define IFR   0x0D
#define IER   0x0E
#define DA2   0x0F

#define MAX_PROTOS 8

typedef struct proto_entry_s
{
    const via_protocol_t *proto;
    void *userdata;
} proto_entry_t;

typedef struct VIACxt_s
{
    uint8_t data_a;
    uint8_t dirmask_a;
    uint8_t data_b;
    uint8_t dirmask_b;


    proto_entry_t protocols[MAX_PROTOS];
} VIACxt_t;

via_t via_init(void)
{
    VIACxt_t *cxt = malloc(sizeof(VIACxt_t));

    if(cxt == NULL)
        return NULL;

    memset(cxt, 0, sizeof(VIACxt_t));
    return (via_t)cxt;
}

void via_cleanup(via_t via)
{
    if(via != NULL)
        free(via);
}

void via_write(via_t handle, uint8_t reg, uint8_t val)
{
    uint8_t i;
    VIACxt_t *cxt = (VIACxt_t *)handle;

    if(cxt == NULL)
        return;

    switch(reg)
    {
        case DDRA:
            cxt->dirmask_a = val;
            break;
        case DATAA:
            cxt->data_a = val;

            for(i=0; i<MAX_PROTOS; ++i)
            {
                if(cxt->protocols[i].proto != NULL)
                    cxt->protocols[i].proto->put(VIA_PORTA, val, cxt->protocols[i].userdata);
            }
            break;
        case DDRB:
            cxt->dirmask_b = val;
            break;
        case DATAB:
            cxt->data_b = val;

            for(i=0; i<MAX_PROTOS; ++i)
            {
                if(cxt->protocols[i].proto != NULL)
                    cxt->protocols[i].proto->put(VIA_PORTB, val, cxt->protocols[i].userdata);
            }
            break;

    }
}

uint8_t via_read(via_t handle, uint8_t reg)
{
    uint8_t out;
    uint8_t i;
    VIACxt_t *cxt = (VIACxt_t *)handle;

    if(cxt == NULL)
        return 0xff;

    switch(reg)
    {
        case DDRA:
            return cxt->dirmask_a;
        case DDRB:
            return cxt->dirmask_b;
        case DATAA:
            // "Pull up" pins before asking protocols to drive them
            out = 0xff;

            for(i=0; i<MAX_PROTOS; ++i)
            {
                if(cxt->protocols[i].proto != NULL)
                    cxt->protocols[i].proto->get(VIA_PORTA, &out, cxt->protocols[i].userdata);
            }

            // Combine input bits with output bits from data register
            out = (out & ~cxt->dirmask_a) | (cxt->data_a & cxt->dirmask_a);
            return out;
        case DATAB:
            // "Pull up" pins before asking protocols to drive them
            out = 0xff;

            for(i=0; i<MAX_PROTOS; ++i)
            {
                if(cxt->protocols[i].proto != NULL)
                    cxt->protocols[i].proto->get(VIA_PORTB, &out, cxt->protocols[i].userdata);
            }

            // Combine input bits with output bits from data register
            out = (out & ~cxt->dirmask_b) | (cxt->data_b & cxt->dirmask_b);
            return out;
    }
    return 0;
}

bool via_register_protocol(via_t handle, const via_protocol_t *protocol, void *userdata)
{
    uint8_t i;
    VIACxt_t *cxt = (VIACxt_t *)handle;

    if(cxt == NULL)
        return false;

    if(protocol == NULL || protocol->put == NULL || protocol->get && NULL)
        return false;

    for(i=0; i<MAX_PROTOS; ++i)
    {
        if(cxt->protocols[i].proto == NULL)
        {
            cxt->protocols[i].proto = protocol;
            cxt->protocols[i].userdata = userdata;
            return true;
        }
    }

    return false;
}

void via_unregister_protocol(via_t handle, const via_protocol_t *protocol)
{
    uint8_t i;
    VIACxt_t *cxt = (VIACxt_t *)handle;

    if(cxt == NULL)
        return;

    if(protocol == NULL)
        return;

    for(i=0; i<MAX_PROTOS; ++i)
    {
        if(cxt->protocols[i].proto == protocol)
        {
            cxt->protocols[i].proto = NULL;
            cxt->protocols[i].userdata = NULL;
        }
    }
}
