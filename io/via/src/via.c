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
#define REG_MAX DA2

#define MAX_PROTOS 8

typedef struct proto_entry_s
{
    const via_protocol_t *proto;
    void *userdata;
} proto_entry_t;

struct via_s
{
    uint8_t data_a;
    uint8_t dirmask_a;
    uint8_t data_b;
    uint8_t dirmask_b;
    bus_cb_handle_t bus_handle;
    cbemu_t emu;
    bool mask_base;
    uint16_t base;

    proto_entry_t protocols[MAX_PROTOS];
};

static void via_bus_write_cb(uint16_t addr, uint8_t val, bus_flags_t flags, void *userdata)
{
    uint8_t reg;
    via_t handle = (via_t)userdata;

    if(handle == NULL)
    {
        return;
    }

    reg = addr - handle->base;

    if(reg > REG_MAX)
    {
        return;
    }

    via_write(handle, reg, val);
}

static uint8_t via_bus_read_cb(uint16_t addr, bus_flags_t flags, void *userdata)
{
    uint8_t reg;
    via_t handle = (via_t)userdata;

    if(handle == NULL)
    {
        return 0xFF;
    }

    reg = addr - handle->base;

    if(reg > REG_MAX)
    {
        return 0xFF;
    }

    return via_read(handle, reg);
}

static const bus_handlers_t via_bus_handlers =
{
    via_bus_write_cb,
    via_bus_read_cb,
    via_bus_read_cb /* TODO Implement a peek callback if for any read operations they may have actions on read. */
};

via_t via_init(io_bus_params_t *bus_params)
{
    via_t cxt;

    if((bus_params != NULL) && ((bus_params->emulator == NULL) || (bus_params->decoder == NULL)))
    {
        return NULL;
    }

    cxt = malloc(sizeof(struct via_s));

    if(cxt == NULL)
        return NULL;

    memset(cxt, 0, sizeof(struct via_s));

    if(bus_params != NULL)
    {
        cxt->bus_handle = emu_bus_register(bus_params->emulator, bus_params->decoder, &via_bus_handlers, cxt);
    }
    return cxt;
}

void via_cleanup(via_t via)
{
    if(via != NULL)
        free(via);
}

void via_write(via_t handle, uint8_t reg, uint8_t val)
{
    uint8_t i;

    if(handle == NULL)
        return;

    switch(reg)
    {
        case DDRA:
            handle->dirmask_a = val;
            break;
        case DATAA:
            handle->data_a = val;

            for(i=0; i<MAX_PROTOS; ++i)
            {
                if(handle->protocols[i].proto != NULL)
                    handle->protocols[i].proto->put(VIA_PORTA, val, handle->protocols[i].userdata);
            }
            break;
        case DDRB:
            handle->dirmask_b = val;
            break;
        case DATAB:
            handle->data_b = val;

            for(i=0; i<MAX_PROTOS; ++i)
            {
                if(handle->protocols[i].proto != NULL)
                    handle->protocols[i].proto->put(VIA_PORTB, val, handle->protocols[i].userdata);
            }
            break;

    }
}

uint8_t via_read(via_t handle, uint8_t reg)
{
    uint8_t out;
    uint8_t i;

    if(handle == NULL)
        return 0xff;

    switch(reg)
    {
        case DDRA:
            return handle->dirmask_a;
        case DDRB:
            return handle->dirmask_b;
        case DATAA:
            // "Pull up" pins before asking protocols to drive them
            out = 0xff;

            for(i=0; i<MAX_PROTOS; ++i)
            {
                if(handle->protocols[i].proto != NULL)
                    handle->protocols[i].proto->get(VIA_PORTA, &out, handle->protocols[i].userdata);
            }

            // Combine input bits with output bits from data register
            out = (out & ~handle->dirmask_a) | (handle->data_a & handle->dirmask_a);
            return out;
        case DATAB:
            // "Pull up" pins before asking protocols to drive them
            out = 0xff;

            for(i=0; i<MAX_PROTOS; ++i)
            {
                if(handle->protocols[i].proto != NULL)
                    handle->protocols[i].proto->get(VIA_PORTB, &out, handle->protocols[i].userdata);
            }

            // Combine input bits with output bits from data register
            out = (out & ~handle->dirmask_b) | (handle->data_b & handle->dirmask_b);
            return out;
    }
    return 0;
}

bool via_register_protocol(via_t handle, const via_protocol_t *protocol, void *userdata)
{
    uint8_t i;

    if(handle == NULL)
        return false;

    if(protocol == NULL || protocol->put == NULL || protocol->get && NULL)
        return false;

    for(i=0; i<MAX_PROTOS; ++i)
    {
        if(handle->protocols[i].proto == NULL)
        {
            handle->protocols[i].proto = protocol;
            handle->protocols[i].userdata = userdata;
            return true;
        }
    }

    return false;
}

void via_unregister_protocol(via_t handle, const via_protocol_t *protocol)
{
    uint8_t i;

    if(handle == NULL)
        return;

    if(protocol == NULL)
        return;

    for(i=0; i<MAX_PROTOS; ++i)
    {
        if(handle->protocols[i].proto == protocol)
        {
            handle->protocols[i].proto = NULL;
            handle->protocols[i].userdata = NULL;
        }
    }
}
