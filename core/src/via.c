#include "via.h"
#include <string.h>
#include <stdio.h>

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

static VIACxt_t VIACxt;

bool via_init(void)
{
    memset(&VIACxt, 0, sizeof(VIACxt));
    return true;
}

void via_write(uint8_t reg, uint8_t val)
{
    uint8_t i;
    switch(reg)
    {
        case DDRA:
            VIACxt.dirmask_a = val;
            break;
        case DATAA:
            VIACxt.data_a = val;

            for(i=0; i<MAX_PROTOS; ++i)
            {
                if(VIACxt.protocols[i].proto != NULL)
                    VIACxt.protocols[i].proto->put(VIA_PORTA, val, VIACxt.protocols[i].userdata);
            }
            break;
        case DDRB:
            VIACxt.dirmask_b = val;
            printf("dirmask write: %02x\n", val);
            break;
        case DATAB:
            VIACxt.data_b = val;

            for(i=0; i<MAX_PROTOS; ++i)
            {
                if(VIACxt.protocols[i].proto != NULL)
                    VIACxt.protocols[i].proto->put(VIA_PORTB, val, VIACxt.protocols[i].userdata);
            }
            break;

    }
}

uint8_t via_read(uint8_t reg)
{
    uint8_t out;
    uint8_t i;

    switch(reg)
    {
        case DDRA:
            return VIACxt.dirmask_a;
        case DDRB:
            return VIACxt.dirmask_b;
        case DATAA:
            // "Pull up" pins before asking protocols to drive them
            out = 0xff;

            for(i=0; i<MAX_PROTOS; ++i)
            {
                if(VIACxt.protocols[i].proto != NULL)
                    VIACxt.protocols[i].proto->get(VIA_PORTA, &out, VIACxt.protocols[i].userdata);
            }

            // Combine input bits with output bits from data register
            out = (out & ~VIACxt.dirmask_a) | (VIACxt.data_a & VIACxt.dirmask_a);
            return out;
        case DATAB:
            // "Pull up" pins before asking protocols to drive them
            out = 0xff;

            for(i=0; i<MAX_PROTOS; ++i)
            {
                if(VIACxt.protocols[i].proto != NULL)
                    VIACxt.protocols[i].proto->get(VIA_PORTB, &out, VIACxt.protocols[i].userdata);
            }

            // Combine input bits with output bits from data register
            out = (out & ~VIACxt.dirmask_b) | (VIACxt.data_b & VIACxt.dirmask_b);
            return out;
    }
    return 0;
}

bool via_register_protocol( const via_protocol_t *protocol, void *userdata)
{
    uint8_t i;

    if(protocol == NULL || protocol->put == NULL || protocol->get && NULL)
        return false;

    for(i=0; i<MAX_PROTOS; ++i)
    {
        if(VIACxt.protocols[i].proto == NULL)
        {
            VIACxt.protocols[i].proto = protocol;
            VIACxt.protocols[i].userdata = userdata;
            return true;
        }
    }

    return false;
}

void via_unregister_protocol(const via_protocol_t *protocol)
{
    uint8_t i;

    if(protocol == NULL)
        return;

    for(i=0; i<MAX_PROTOS; ++i)
    {
        if(VIACxt.protocols[i].proto == protocol)
        {
            VIACxt.protocols[i].proto = NULL;
            VIACxt.protocols[i].userdata = NULL;
        }
    }
}
