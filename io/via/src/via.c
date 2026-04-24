#include "via.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "util.h"

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

typedef struct
{
    via_event_callback_t callback;
    void *userdata;
    listnode_t node;
} via_callback_info_t;

struct via_s
{
    uint8_t ira;
    uint8_t ora;
    uint8_t ddra;

    uint8_t irb;
    uint8_t orb;
    uint8_t ddrb;

    bus_cb_handle_t bus_handle;
    cbemu_t emu;
    bool mask_base;
    uint16_t base;
    listnode_t callbacks;
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

static void via_make_callbacks(via_t handle, const via_event_data_t *event)
{
    via_callback_info_t *info;
    listnode_t *node;

    list_iterate(&handle->callbacks, node)
    {
        info = list_container(node, via_callback_info_t, node);

        info->callback(handle, event, info->userdata);
    }
}

via_t via_init(void)
{
    via_t cxt;

    cxt = malloc(sizeof(struct via_s));

    if(cxt == NULL)
        return NULL;

    memset(cxt, 0, sizeof(struct via_s));
    list_init(&cxt->callbacks);

    return cxt;
}

void via_cleanup(via_t via)
{
    if(via == NULL)
    {
        return;
    }

    list_free_offset(&via->callbacks, via_callback_info_t, node);

    free(via);
}

bool via_register(via_t handle, const cbemu_t emu, const bus_decode_params_t *decoder, uint16_t base)
{
    if((handle == NULL) || (emu == NULL) || (decoder == NULL))
    {
        return false;
    }

    handle->bus_handle = emu_bus_register(emu, decoder, &via_bus_handlers, handle);

    if(handle->bus_handle != NULL)
    {
        handle->emu = emu;
        handle->base = base;
    }

    return (handle->bus_handle != NULL);
}

void via_write(via_t handle, uint8_t reg, uint8_t val)
{
    uint8_t old_val;
    bool dispatch = false;
    via_event_data_t event;

    if(handle == NULL)
        return;

    memset(&event, 0, sizeof(event));
    event.type = VIA_EV_PORT_CHANGE;

    switch(reg)
    {
        case DDRA:
            if(handle->ddra != val)
            {
                handle->ddra = val;
                dispatch = true;
                event.data.port = VIA_PORTA;
            }
            break;
        case DATAA:
            if(handle->ora != val)
            {
                handle->ora = val;
                dispatch = true;
                event.data.port = VIA_PORTA;
            }
            break;
        case DDRB:
            if(handle->ddrb != val)
            {
                handle->ddrb = val;
                dispatch = true;
                event.data.port = VIA_PORTB;
            }
            break;
        case DATAB:
            if(handle->orb != val)
            {
                handle->orb = val;
                dispatch = true;
                event.data.port = VIA_PORTB;
            }
            break;
    }

    if(dispatch)
    {
        via_make_callbacks(handle, &event);
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
            return handle->ddra;
        case DDRB:
            return handle->ddrb;

        case DATAA:
            /* In reality, PORTA is a bit special in that output pins always read back
             * the physical pin state rather than the current output register state. In
             * practice, this is difficult/not very meaningful to emulate, so we won't. */
            return ((handle->ira & ~handle->ddra) | (handle->ora & handle->ddra));
        case DATAB:
            return ((handle->irb & ~handle->ddrb) | (handle->orb & handle->ddrb));
    }
    return 0;
}

via_cb_handle_t via_register_callback(via_t handle, via_event_callback_t callback, void *userdata)
{
    via_callback_info_t *info;

    if((handle == NULL) || (callback == NULL))
    {
        return NULL;
    }

    info = malloc(sizeof(via_callback_info_t));

    if(info != NULL)
    {
        memset(info, 0, sizeof(via_callback_info_t));

        info->callback = callback;
        info->userdata = userdata;

        list_add_tail(&handle->callbacks, &info->node);
    }

    return info;
}

void via_unregister_callback(via_t handle, via_cb_handle_t cb_handle)
{
    via_callback_info_t *info = (via_callback_info_t *)cb_handle;

    if((handle == NULL) || (cb_handle == NULL))
    {
        return;
    }

    list_remove(&info->node);

    free(info);
}

void via_write_data_port(via_t handle, bool porta, uint8_t mask, uint8_t data)
{
    if((handle == NULL) || (mask == 0))
    {
        return;
    }

    /* TODO: handle latching. For now, just update ira directly. */
    if(porta)
    {
        handle->ira = (handle->ira & ~mask) | (data & mask);
    }
    else
    {
        handle->irb = (handle->irb & ~mask) | (data & mask);
    }
}

void via_write_ctrl(via_t handle, via_ctrl_pin_t pin, bool val)
{
    /* Not supported yet. */
}

uint8_t via_read_data_port(via_t handle, bool porta)
{
    if(handle == NULL)
    {
        return 0;
    }

    /* For now, merge the output pins from the via with the current input states externally. */
    if(porta)
    {
        return (handle->ora & handle->ddra) | (handle->ira & ~handle->ddra);
    }
    else
    {
        return (handle->orb & handle->ddrb) | (handle->irb & ~handle->ddrb);
    }
}

bool via_read_ctrl(via_t handle, via_ctrl_pin_t pin)
{
    /* Not supported yet. */
    return false;
}
