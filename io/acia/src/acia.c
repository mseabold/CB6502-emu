#include <stdlib.h>
#include <string.h>

#include "acia.h"
#include "log.h"

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

#define ACIA_STATUS_SW_RESET_MASK 0x04

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

static const unsigned int ACIA_SBR_TABLE[] =
{
    16,
    36864,
    24576,
    16769,
    13704,
    12288,
    6144,
    3072,
    1536,
    1024,
    768,
    512,
    364,
    256,
    192,
    96
};

static const uint8_t ACIA_BIT_LEN_TABLE[] =
{
    8,
    7,
    6,
    5
};

typedef struct
{
    uint8_t sbr : 4;
    uint8_t rcs : 1;
    uint8_t wl : 2;
    uint8_t sbn : 1;
} acia_ctrl_reg_fields_t;

struct acia_s
{
    union {
        acia_ctrl_reg_fields_t fields;
        uint8_t val;
    } ctl_reg;
    uint8_t cmd_reg;
    uint8_t stat_reg;

    bool irq_pend;
    cbemu_t emu;
    bus_cb_handle_t bus_handle;
    uint16_t base;
    clk_t bit_clock;
    clock_cb_handle_t clock_cb;
    bus_signal_voter_t voter;

    const acia_trans_interface_t *transport;
    void *trans_param;
    void *trans_handle;

    unsigned int tx_ticks;
    unsigned int rx_ticks;
    uint8_t write_val;
    uint8_t read_val;
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
    acia_tick((acia_t)userdata);
}

static const bus_handlers_t acia_bus_handlers =
{
    acia_bus_write_cb,
    acia_bus_read_cb,
    acia_bus_read_cb
};

static unsigned int acia_get_ticks_per_word(acia_t handle)
{
    unsigned int data_ticks;
    unsigned int stop_ticks;
    unsigned int bit_ticks;


    bit_ticks = ACIA_SBR_TABLE[handle->ctl_reg.fields.sbr];
    data_ticks = (ACIA_BIT_LEN_TABLE[handle->ctl_reg.fields.wl] + 1) * bit_ticks;

    if(handle->ctl_reg.fields.sbn)
    {
        if(handle->ctl_reg.fields.wl == 3)
        {
            stop_ticks = (3 * bit_ticks) >> 1;
        }
        else
        {
            stop_ticks = bit_ticks * 2;
        }
    }
    else
    {
        stop_ticks = bit_ticks;
    }

    return data_ticks + stop_ticks;
}

acia_t acia_init(cbemu_t emu, const acia_trans_interface_t *transport, void *transport_params, clk_t bit_clock)
{
    bool error = false;
    acia_t cxt;

    if(transport == NULL || emu == NULL)
    {
        return NULL;
    }

    cxt = malloc(sizeof(struct acia_s));

    if(cxt == NULL)
        return NULL;

    memset(cxt, 0, sizeof(struct acia_s));

    cxt->emu = emu;
    cxt->transport = transport;
    cxt->trans_param = transport_params;
    cxt->voter = BUS_SIGNAL_INVALID_VOTER;

    /* W65C51 always returns TRDE set. */
    cxt->stat_reg = ACIA_STATUS_TDRE;

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

bool acia_register(acia_t handle, const bus_decode_params_t *decoder, uint16_t base_addr, bool interrupt)
{
    bool error = false;

    if(handle == NULL)
    {
        return false;
    }

    if(decoder)
    {
        handle->bus_handle = emu_bus_register(handle->emu, decoder, &acia_bus_handlers, handle);

        if(handle->bus_handle != NULL)
        {
            handle->base = base_addr;
        }
        else
        {
            error = true;
        }
    }

    if(interrupt && !error)
    {
        handle->voter = emu_bus_register_sig_voter(handle->emu);

        if(handle->voter == BUS_SIGNAL_INVALID_VOTER)
        {
            error = true;
        }
    }

    if(error)
    {
        if(handle->bus_handle != NULL)
        {
            emu_bus_unregister(handle->emu, handle->bus_handle);
            handle->bus_handle = NULL;
        }
    }


    return !error;
}

void acia_write(acia_t handle, uint8_t reg, uint8_t val)
{
    if(handle == NULL)
        return;

    switch(reg)
    {
        case ACIA_RS_TX_DATA:
            /* Don't allow Tx if DTRB is not set. */
            if(handle->cmd_reg & ACIA_CMD_DTR_MASK)
            {
                /* TODO: What happens here exactly when writing while a tx is in progress. Fully accurate behavior would
                 * need testing on HW. For now, just ignore it with a warning log. */
                if(handle->tx_ticks > 0)
                {
                    log_print(lWARNING, "ACIA: Write to TX Data occurred during active tx byte\n");
                    return;
                }

                handle->write_val = val;
                handle->tx_ticks = acia_get_ticks_per_word(handle);
            }
            break;
        case ACIA_RS_SW_RESET:
            /* Reset affected bits to 0. */
            handle->stat_reg &= ~ACIA_STATUS_SW_RESET_MASK;
            handle->cmd_reg &= ~ACIA_CMD_SW_RESET_MASK;
            break;
        case ACIA_RS_CTRL:
            handle->ctl_reg.val = val;
            break;
        case ACIA_RS_CMD:
            handle->cmd_reg = val;

            if(!(handle->cmd_reg & ACIA_CMD_DTR_MASK))
            {
                /* Cut off any transmission of DTR is cleared. */
                handle->tx_ticks = 0;
            }
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
            if(handle->stat_reg & ACIA_STATUS_RDRF)
            {
                ret = handle->read_val;
                handle->stat_reg &= ~ACIA_RX_READ_CLEAR_BITS;

            }
            break;
        case ACIA_RS_STATUS:
            ret = handle->stat_reg;
            handle->stat_reg &= ~ACIA_STATUS_IRQ;

            /* TODO: Verify in HW that clearing STAT bit 7 actually clears the IRQB signal
             * and not the ultimate source bits (RDRF, etc). */
            if(handle->voter != BUS_SIGNAL_INVALID_VOTER)
            {
                emu_bus_sig_vote(handle->emu, handle->voter, BUS_SIG_IRQ, false);
            }
            break;
        case ACIA_RS_CTRL:
            ret = handle->ctl_reg.val;
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

    if(handle->voter != BUS_SIGNAL_INVALID_VOTER)
    {
        emu_bus_unregister_sig_voter(handle->emu, handle->voter);
    }

    if(handle->clock_cb != NULL)
    {
        clock_unregister_tick(handle->clock_cb);
    }
    free(handle);
}

void acia_tick(acia_t handle)
{
    if(handle == NULL)
    {
        return;
    }

    if(handle->rx_ticks > 0)
    {
        /* Rx is in progress, decrement the tick count. */
        handle->rx_ticks--;

        if(handle->rx_ticks == 0)
        {
            /* The entire word time has been consumed, so flag the data is now available. */
            if(!(handle->stat_reg & ACIA_STATUS_RDRF))
            {
                handle->stat_reg |= ACIA_STATUS_RDRF;
                handle->read_val = handle->transport->read(handle->trans_handle);

                /* For now, this is the only feature we support that can trigger an interrupt. */
                if((handle->voter != BUS_SIGNAL_INVALID_VOTER) && ((handle->cmd_reg & ACIA_CMD_IRD_MASK) == (ACIA_CMD_IRD_ENABLED)))
                {
                    handle->stat_reg |= ACIA_STATUS_IRQ;
                    emu_bus_sig_vote(handle->emu, handle->voter, BUS_SIG_IRQ, true);
                }
            }
            else
            {
                /* An rx byte has completed with the previous byte having not yet been read by the CPU.
                 * Flag an Overflow condition and read/discard the overflow byte. */
                handle->stat_reg |= ACIA_STATUS_OVER;
                (void)handle->transport->read(handle->trans_handle);
            }

        }

    }
    else if((handle->transport->available(handle->trans_handle)) && (handle->cmd_reg & ACIA_CMD_DTR_MASK))
    {
        /* No current data pending, but the transport has a byte available. Start ticking the bit clock for rx. */
        handle->rx_ticks = acia_get_ticks_per_word(handle);
    }

    if(handle->tx_ticks > 0)
    {
        handle->tx_ticks--;

        if(handle->tx_ticks == 0)
        {
            handle->transport->write(handle->trans_handle, handle->write_val);
        }
    }
}
