#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "acia.h"

#define ACIA_RS_TX_DATA 0x00
#define ACIA_RS_RX_DATA 0x00
#define ACIA_RS_SW_RESET 0x01
#define ACIA_RS_STATUS   0x01
#define ACIA_RS_CMD      0x02
#define ACIA_RS_CTRL     0x03
#define ACIA_REG_MAX     ACIA_RS_CTRL

#define ACIA_STATUS_IRQ   0x80
#define ACIA_STATUS_DSRB  0x40
#define ACIA_STATUS_DCDB  0x20
#define ACIA_STATUS_TDRE  0x10
#define ACIA_STATUS_RDRF  0x08
#define ACIA_STATUS_OVER  0x04
#define ACIA_STATUS_FRAME 0x02
#define ACIA_STATUS_PAR   0x01

#define ACIA_RX_READ_CLEAR_BITS (ACIA_STATUS_RDRF | ACIA_STATUS_OVER | ACIA_STATUS_FRAME | ACIA_STATUS_PAR)

#define ACIA_CTRL_SBN_MASK 0x80
#define ACIA_CTRL_SBN_1_BIT 0x00
#define ACIA_CTRL_SBN_2_BIT 0x80

#define ACIA_CTRL_WL_MASK 0x60
#define ACIA_CTRL_WL_8_BITS 0x00
#define ACIA_CTRL_WL_7_BITS 0x20
#define ACIA_CTRL_WL_6_BITS 0x40
#define ACIA_CTRL_WL_5_BITS 0x60

#define ACIA_CTRL_RCS_MASK 0x10
#define ACIA_CTRL_RCS_EXT  0x00
#define ACIA_CTRL_RCS_BAUD 0x10

#define ACIA_CTRL_SBR_MASK 0x0f
/* Unused currently, so skip defining. */

#define ACIA_CTRL_HW_RESET_VAL 0x00

#define ACIA_CMD_PMC_MASK 0xc0
#define ACIA_CMD_PME_MASK 0x20
#define ACIA_CMD_REM_MASK 0x10
#define ACIA_CMD_TIC_MASK 0x0c
#define ACIA_CMD_IRD_MASK 0x02
#define ACIA_CMD_DTR_MASK 0x01

#define ACIA_CMD_IRD_ENABLED  0x00
#define ACIA_CMD_IRD_DISABLED 0x02

#define ACIA_CMD_HW_RESET_VAL 0x00
#define ACIA_CMD_SW_RESET_MASK 0x1f
#define ACIA_CMD_SW_RESET_VAL  0x00

#define ACIA_RX_BUF_SIZE 256

struct acia_s
{
    uint8_t ctl_reg;
    uint8_t cmd_reg;
    uint8_t stat_reg;

    bool irq_pend;
    cbemu_t emu;
    bus_cb_handle_t bus_handle;
    uint16_t base;
    clk_t bit_clock;
    clock_cb_handle_t clock_cb;

    const acia_trans_interface_t *transport;
    void *trans_param;
    void *trans_handle;
};

static void acia_bus_write_cb(uint16_t addr, uint8_t val, bus_flags_t flags, void *userdata)
{
    acia_t handle = (acia_t)userdata;
    uint16_t reg;

    if(handle == NULL)
    {
        return;
    }

    reg = addr - handle->base;

    if(reg >= ACIA_REG_MAX)
    {
        return;
    }

    acia_write(handle, reg, val);
}

static uint8_t acia_bus_read_cb(uint16_t addr, bus_flags_t flags, void *userdata)
{
    acia_t handle = (acia_t)userdata;
    uint16_t reg;

    if(handle == NULL)
    {
        return 0xFF;
    }

    reg = addr - handle->base;

    if(reg >= ACIA_REG_MAX)
    {
        return 0xFF;
    }

    return acia_read(handle, reg);
}

static void acia_tick_cb(clk_t clock, void *userdata)
{
}

static const bus_handlers_t acia_bus_handlers =
{
    acia_bus_write_cb,
    acia_bus_read_cb,
    acia_bus_read_cb
};

acia_t acia_init(const acia_trans_interface_t *transport, void *transport_params, clk_t bit_clock)
{
    bool error = false;
    acia_t cxt;

    if(transport == NULL)
    {
        return NULL;
    }

    cxt = malloc(sizeof(struct acia_s));

    if(cxt == NULL)
        return NULL;

    memset(cxt, 0, sizeof(struct acia_s));

    cxt->transport = transport;
    cxt->trans_param = transport_params;

    cxt->trans_handle = transport->init(transport_params);

    if(cxt->trans_handle == NULL)
    {
        error = true;
    }

    if(!error)
    {
        cxt->bit_clock = bit_clock;
        cxt->clock_cb = clock_register_tick(cxt->bit_clock, acia_tick_cb, cxt);

        if(cxt->clock_cb == NULL)
        {
            error = true;
        }
    }

    if(error)
    {
        acia_cleanup(cxt);
        cxt = NULL;
    }

    return cxt;
}

bool acia_register(acia_t handle, const cbemu_t emu, const bus_decode_params_t *decoder, uint16_t base_addr)
{
    if((handle == NULL) || (emu == NULL) || (decoder == NULL))
    {
        return false;
    }

    handle->bus_handle = emu_bus_register(emu, decoder, &acia_bus_handlers, handle);

    if(handle->bus_handle != NULL)
    {
        handle->base = base_addr;
        handle->emu = emu;
    }

    return (handle->bus_handle != NULL);
}

void acia_write(acia_t handle, uint8_t reg, uint8_t val)
{
    if(handle == NULL)
        return;

    switch(reg)
    {
        case ACIA_RS_TX_DATA:
            handle->transport->write(handle->trans_handle, val);
            break;
        case ACIA_RS_SW_RESET:
            /* TODO */
            break;
        case ACIA_RS_CTRL:
            handle->ctl_reg = val;
            break;
        case ACIA_RS_CMD:
            handle->cmd_reg = val;
            break;
    }
}

uint8_t acia_read(acia_t handle, uint8_t reg)
{
    uint8_t ret = 0xff;

    if(handle == NULL)
        return 0xff;

    switch(reg)
    {
        case ACIA_RS_RX_DATA:
            ret = handle->transport->read(handle->trans_handle);
            break;
        case ACIA_RS_STATUS:
            ret = handle->stat_reg;
            handle->stat_reg &= ~ACIA_STATUS_IRQ;

            /* TODO, For now, just flag RDRF if there is data available when status
             *       is read. This will likely change with integration of ticks.
             */
            if(handle->transport->available(handle->trans_handle))
            {
                ret |= ACIA_STATUS_RDRF;
            }
            else
            {
                ret &= ~ACIA_STATUS_RDRF;
            }
            break;
        case ACIA_RS_CTRL:
            ret = handle->ctl_reg;
            break;
        case ACIA_RS_CMD:
            ret = handle->cmd_reg;
            break;
    }

    return ret;
}

void acia_cleanup(acia_t handle)
{
    if(handle == NULL)
        return;

    if(handle->trans_handle != NULL)
    {
        handle->transport->cleanup(handle->trans_handle);
    }

    if(handle->bus_handle != NULL)
    {
        emu_bus_unregister(handle->emu, handle->bus_handle);
    }

    if(handle->clock_cb != NULL)
    {
        clock_unregister_tick(handle->clock_cb);
    }
    free(handle);
}
