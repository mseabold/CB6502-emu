#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "bus.h"
#include "util.h"

#include "via.h"
#include "log.h"

/*
 * TODO:
 *  Some edge cases of the VIA need to be verified on actual HW to determine how they act.
 *  I'm going to document all of those questions here so that they are listed in one place
 *  and I will reference these where appropriate in the code.
 *
 *  HW1) CA1 can be used for handshaking and latching. Hwo does PCR affect this? I believe PCR
 *       controls the edfe of IRA latching, but does it affect DATA_TAKEN for handshaking?
 *  HW2) Previous testing indicating changing DDR with latching enabled trigger in IR latch.
 *       Does this apply to pins already configured as inputs?
 */

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

#define MERGE_BITS(_val1, _val2, _val1_mask) (((_val1) & (_val1_mask)) | ((_val2) & (~_val1_mask)))

typedef struct
{
    via_event_callback_t callback;
    void *userdata;
    listnode_t node;
} via_callback_info_t;

typedef struct
{
    uint8_t pa_latch : 1;
    uint8_t pb_latch : 1;
    uint8_t sr_ctrl : 3;
    uint8_t t2_ctrl : 1;
    uint8_t t1_ctrl : 2;
} acr_bits_t;

typedef struct
{
    uint8_t ca1_ctrl : 1;
    uint8_t ca2_ctrl : 3;
    uint8_t cb1_ctrl : 1;
    uint8_t cb2_ctrl : 3;
} pcr_bits_t;

typedef union
{
    uint8_t val;
    acr_bits_t bits;
} acr_t;

typedef union
{
    uint8_t val;
    pcr_bits_t bits;
} pcr_t;

typedef struct
{
    uint8_t ir;
    uint8_t or;
    uint8_t ddr;
    uint8_t pin_in;
    bool c1_in;
    bool c2_in;
} via_port_state_t;

struct via_s
{
    via_port_state_t porta;
    via_port_state_t portb;

    acr_t acr;
    pcr_t pcr;

    uint8_t ier;

    bus_cb_handle_t bus_handle;
    bus_signal_voter_t voter;
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

static bool via_ddr_write(via_t handle, via_port_state_t *port, uint8_t val)
{
    bool processed = false;
    uint8_t bits_to_latch;

    if(port->ddr != val)
    {
        /* HW Testing has shown then switch a pin to input
         * while latching is enabled triggers a latch of current
         * pin state.
         *
         * *See HW2* */
        if(handle->acr.bits.pa_latch)
        {
            /* Check which ddra bits have change 1->0 */
            bits_to_latch = (port->ddr ^ val) & ~val;

            /* Latch the bits to the current pin input state. */
            port->ir = MERGE_BITS(port->pin_in, port->ir, bits_to_latch);
        }

        port->ddr = val;
        processed = true;
    }

    return processed;
}

static void via_handle_c1_edge(via_t handle, bool porta)
{
    via_port_state_t *port = porta ? &handle->porta : &handle->portb;
    bool latch = porta ? handle->acr.bits.pa_latch : handle->acr.bits.pb_latch;
    uint8_t ca_ctrl = porta ? handle->pcr.bits.ca1_ctrl : handle->pcr.bits.cb1_ctrl;
    bool posedge = ca_ctrl != 0;

    /* Check for a latch condition. We know if we're here that an edge occurred,
     * so check the newly updated state to see if it matches the configured edge. */
    if((latch) && (port->c1_in == posedge))
    {
        /* Latch the current pin state into ir for inputs. */
        port->ir = port->pin_in;
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
    cxt->voter = BUS_SIGNAL_INVALID_VOTER;

    return cxt;
}

void via_cleanup(via_t via)
{
    if(via == NULL)
    {
        return;
    }

    if(via->voter != BUS_SIGNAL_INVALID_VOTER)
    {
        emu_bus_unregister_sig_voter(via->emu, via->voter);
    }

    if(via->bus_handle != NULL)
    {
        emu_bus_unregister(via->emu, via->bus_handle);
    }

    list_free_offset(&via->callbacks, via_callback_info_t, node);

    free(via);
}

bool via_register(via_t handle, const cbemu_t emu, const bus_decode_params_t *decoder, uint16_t base, bool irq)
{
    bool error = false;

    if((handle == NULL) || (emu == NULL) || (decoder == NULL) || (handle->bus_handle != NULL) || (handle->voter != BUS_SIGNAL_INVALID_VOTER))
    {
        return false;
    }

    handle->emu = emu;
    handle->bus_handle = emu_bus_register(emu, decoder, &via_bus_handlers, handle);

    if(handle->bus_handle != NULL)
    {
        handle->base = base;
    }
    else
    {
        error = true;
    }

    if(irq && !error)
    {
        handle->voter = emu_bus_register_sig_voter(handle->emu);

        if(handle->voter == BUS_SIGNAL_INVALID_VOTER)
        {
            error = true;

            if(handle->bus_handle != NULL)
            {
                emu_bus_unregister(emu, handle->bus_handle);
            }
        }
    }

    return !error;
}

void via_write(via_t handle, uint8_t reg, uint8_t val)
{
    uint8_t old_val;
    uint8_t bits_to_latch;
    bool dispatch = false;
    via_event_data_t event;

    if(handle == NULL)
        return;

    memset(&event, 0, sizeof(event));
    event.type = VIA_EV_PORT_CHANGE;

    switch(reg)
    {
        case DDRA:
            if(via_ddr_write(handle, &handle->porta, val))
            {
                dispatch = true;
                event.data.port = VIA_PORTA;
            }
            break;
        case DATAA:
            if(handle->porta.or != val)
            {
                handle->porta.or = val;
                dispatch = true;
                event.data.port = VIA_PORTA;
            }
            break;
        case DDRB:
            if(via_ddr_write(handle, &handle->portb, val))
            {
                dispatch = true;
                event.data.port = VIA_PORTB;
            }
            break;
        case DATAB:
            if(handle->portb.or != val)
            {
                handle->portb.or = val;
                dispatch = true;
                event.data.port = VIA_PORTB;
            }
            break;
        case ACR:
            handle->acr.val = val;
            break;
        case PCR:
            handle->pcr.val = val;
            break;
        case IER:
            if(val & 0x80)
            {
                handle->ier |= (val & 0x7f);
            }
            else
            {
                handle->ier &= ~(val & 0x7f);
            }
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
            return handle->porta.ddr;
        case DDRB:
            return handle->portb.ddr;

        case DATAA:
            /* In reality, PORTA is a bit special in that output pins always read back
             * the physical pin state rather than the current output register state. In
             * practice, this is difficult/not very meaningful to emulate, so we won't. */
            return MERGE_BITS(handle->porta.or, handle->porta.ir, handle->porta.ddr);
        case DATAB:
            return MERGE_BITS(handle->portb.or, handle->portb.ir, handle->portb.ddr);
        case ACR:
            return handle->acr.val;
        case PCR:
            return handle->pcr.val;
        case IER:
            return handle->ier;
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
    via_port_state_t *port;
    bool latch;

    log_print(lDEBUG, "via_write_data_port(%p, %u, %02x, %02x)\n", handle, porta, mask, data);

    if((handle == NULL) || (mask == 0))
    {
        return;
    }

    port = porta ? &handle->porta : &handle->portb;
    latch = porta ? handle->acr.bits.pa_latch : handle->acr.bits.pb_latch;

    port->pin_in = MERGE_BITS(data, port->pin_in, mask);

    log_print(lDEBUG, "Write %s to %02x\n", porta ? "porta": "portb", port->pin_in);

    /* Updare irx directly if latching is disabled. */
    if(!latch)
    {
        port->ir = port->pin_in;
    }
}

void via_write_ctrl(via_t handle, via_ctrl_pin_t pin, bool val)
{
    bool cur_state;

    if(handle == NULL)
    {
        return;
    }

    switch(pin)
    {
        case VIA_CA1:
            cur_state = handle->porta.c1_in;
            break;
        case VIA_CA2:
            cur_state = handle->porta.c2_in;
            break;
        case VIA_CB1:
            cur_state = handle->portb.c1_in;
            break;
        case VIA_CB2:
            cur_state = handle->portb.c2_in;
            break;
    }

    if(val == cur_state)
    {
        /* All input behavior is triggered on edges, so nothing
         * to do without an edge. */
        return;
    }

    switch(pin)
    {
        case VIA_CA1:
            handle->porta.c1_in = val;

            via_handle_c1_edge(handle, true);
            break;
        case VIA_CA2:
            handle->porta.c2_in = val;
            break;
        case VIA_CB1:
            handle->portb.c1_in = val;

            via_handle_c1_edge(handle, false);
            break;
        case VIA_CB2:
            handle->portb.c2_in = val;
            break;
    }

    /* Check for CA1 latching edges. We already know this is an edge,
     * so check the new state against the configured edge. */
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
        return MERGE_BITS(handle->porta.or, handle->porta.pin_in, handle->porta.ddr);
    }
    else
    {
        return MERGE_BITS(handle->portb.or, handle->portb.pin_in, handle->portb.ddr);
    }
}

bool via_read_ctrl(via_t handle, via_ctrl_pin_t pin)
{
    /* Not supported yet. */
    return false;
}
