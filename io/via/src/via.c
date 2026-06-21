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
 *
 *  HW2) Previous testing indicating changing DDR with latching enabled trigger in IR latch.
 *       Does this apply to pins already configured as inputs?
 *
 *  HW3) Does a CA1/CB1 configured edge still trigger CA1/CB1 IFR if latching is disabled?
 *       Similarly, does a read of PORTA/BORTB always clear this regardless of latch setting?
 *
 *  HW4) Does Read handshaking on PORTA occur on every IRA read regardless of DDRA/latching?
 *       Does Write handshaking then also occur on every write? How do these two interplay?
 *       Note, this is not a question on PORTB as it only supports Write handshaking
 *
 *  HW5) Are handshaking outputs inteernally used/consistent despite CX2 setting? I.e. does
 *       reading/writing generate the signal internally, and then if CX2 setting changes while
 *       signal is still pending, the signal is output as set? Or is it only set in that output mode?
 *       Similarly, if the output is set and then CX2 control is disabled, is the output signal cleared
 *       or does the output singal remain and will be applied again of CX2 is switched back?
 *       For now, assuming the latter for ease of implementation as that the standard use case would
 *       be to set up the mode prior to using it.
 *
 *  HW6) Does Read Handshaking require an initial Data Ready signal on CA1 to activate, or is
 *       it triggered on every PORTA read?
 *
 *  HW7) Does DDR affect handshaking?
 *
 *  HW8) Datasheet for WDC conflicts for T1L-H. Register table says "high order latches", but description
 *       under T1 Operation says "high order counter"
 *
 *  HW9) It's not clearly stated, but datasheet seems to imply that timer counters seem decrement on every
 *       PHI2 edge regardless of timer state. Will implement this way, but need to check for sure. For T1
 *       the implication is also that T1L->T1C loading happens at every underflow.
 *
 *  HW10) Datasheet shows T1 in continuous mode having IFR/PB7 set on PHI2 posedge for iteration 1 and
 *        negedge for iteration 2. This doesn't make much sense and contradicts the timing diagram showing
 *        N+1.5 cycles for iteration 1 and N+2 for iteration 2 implying the same edge. For now, assumption is
 *        that the negedge in the diagram is incorrect.
 *
 *  HW11) Multiple T1-PB7 questions wrapped up here:
 *      1 - What is the priority of T1 control over PB7 vs PORTB with DDRB7 set?
 *      2 - Does T1 PB7 control work if DDRB7 is 0?
 *      3 - Is T1 PB7 control an internal state regardless of ACR setting? I.e. is it toggling/being
 *          set while the timer is running even if ACR7 is 0, and then setting ACR7 to 1 immediately
 *          reflects this value? Or is the state re-initialized on chanding ACR. Current assumption is
 *          internal state
 *      4 - Datasheet diagrams always show PB7 starting high and then going low on T1 start. What if T1 is de-armed.
 *          switched to one-shot while PB7 is low? Does it remain low? Does starting a new one-shot make it go high
 *          for pulse mode?
 *      5 - Verify that re-arming timer before it expired does not invert PB7, only expiration.
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

#define IFR_CA2 0x01
#define IFR_CA1 0x02
#define IFR_SR  0x04
#define IFR_CB2 0x08
#define IFR_CB1 0x10
#define IFR_T2  0x20
#define IFR_T1  0x40

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
    uint8_t t1_mode : 1;
    uint8_t t1_pb7 : 1;
} acr_bits_t;

#define VIA_ACR_SR_DISABLED     0x0
#define VIA_ACR_SR_IN_T2        0x1
#define VIA_ACR_SR_IN_PHI2      0x2
#define VIA_ACR_SR_IN_EXT_CLK   0x3
#define VIA_ACR_SR_OUT_FREE_T2  0x4
#define VIA_ACR_SR_OUT_T2       0x5
#define VIA_ACR_SR_OUT_PHI2     0x6
#define VIA_ACR_SR_OUT_EXT_CLK  0x7

#define VIA_ACR_T2_TIMED        0x0
#define VIA_ACR_T2_PB6_PULSE    0x1

#define VIA_ACR_T1_MODE_ONE_SHOT    0x0
#define VIA_ACR_T1_MODE_CONT        0x1
#define VIA_ACR_T1_PB7_DISABLED     0x0
#define VIA_ACR_T1_PB7_ENABLED      0x1

typedef struct
{
    uint8_t ca1_ctrl : 1;
    uint8_t ca2_ctrl : 3;
    uint8_t cb1_ctrl : 1;
    uint8_t cb2_ctrl : 3;
} pcr_bits_t;

#define VIA_C1_CTRL_NEG_EDGE   0x00
#define VIA_C1_CTRL_POS_EDGE   0x01

#define VIA_C2_CTRL_INPUT_NEG_EDGE      0x00
#define VIA_C2_CTRL_INPUT_IND_NEG_EDGE  0x01
#define VIA_C2_CTRL_INPUT_POS_EDGE      0x02
#define VIA_C2_CTRL_INPUT_IND_POS_EDGE  0x03
#define VIA_C2_CTRL_OUTPUT_HANDSAKE     0x04
#define VIA_C2_CTRL_OUTPUT_PULSE        0x05
#define VIA_C2_CTRL_OUTPUT_LOW          0x06
#define VIA_C2_CTRL_OUTPUT_HIGH         0x07

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
    bool c2_out;
} via_port_state_t;

struct via_s
{
    via_port_state_t porta;
    via_port_state_t portb;

    acr_t acr;
    pcr_t pcr;
    uint8_t ier;
    uint8_t ifr;

    uint16_t t1l;
    uint16_t t1c;
    bool t1pb7;

    uint32_t flags;

    clock_cb_handle_t clk_cb;
    bus_cb_handle_t bus_handle;
    bus_signal_voter_t voter;
    cbemu_t emu;
    bool mask_base;
    uint16_t base;
    listnode_t callbacks;
};

#define VIA_FLAG_CA2_TRIG_PEND          0x0001
#define VIA_FLAG_CA2_PULSE_PEND         0x0002
#define VIA_FLAG_CA2_READ_TRIG_PEND     0x0004
#define VIA_FLAG_CA2_READ_PULSE_PEND    0x0008
#define VIA_FLAG_CB2_TRIG_PEND          0x0010
#define VIA_FLAG_CB2_PULSE_PEND         0x0020
#define VIA_FLAG_T1_ARMED               0x0040
#define VIA_FLAG_T2_ARMED               0x0080
#define VIA_FLAG_T1_WRITE_PEND          0x0100
#define VIA_FLAG_T2_WRITE_PEND          0x0200

/* Helpers for readability */
#define VIA_CHECK_FLAG(_handle, _flag)  ((_handle)->flags & (_flag))
#define VIA_SET_FLAG(_handle, _flag)    ((_handle)->flags |= (_flag))
#define VIA_CLEAR_FLAG(_handle, _flag)  ((_handle)->flags &= ~(_flag))

#define VIA_FLAGS_CA_SHAKE_PEND     (VIA_FLAG_CA2_TRIG_PEND | VIA_FLAG_CA2_PULSE_PEND | VIA_FLAG_CA2_READ_PULSE_PEND)
#define VIA_FLAGS_CB_SHAKE_PEND     (VIA_FLAG_CB2_TRIG_PEND | VIA_FLAG_CB2_PULSE_PEND)
#define VIA_FLAGS_HANDSHAKE_PEND    (VIA_FLAGS_CA_SHAKE_PEND | VIA_FLAGS_CB_SHAKE_PEND)

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

static inline bool via_is_independent_int(via_t handle, bool porta)
{
    uint8_t c2_ctrl = porta ? handle->pcr.bits.ca2_ctrl : handle->pcr.bits.cb2_ctrl;

    return ((c2_ctrl == VIA_C2_CTRL_INPUT_IND_NEG_EDGE) || (c2_ctrl == VIA_C2_CTRL_INPUT_IND_POS_EDGE));
}

static inline bool via_is_handshake_output(via_t handle, bool porta)
{
    uint8_t c2_ctrl = porta ? handle->pcr.bits.ca2_ctrl : handle->pcr.bits.cb2_ctrl;

    return ((c2_ctrl == VIA_C2_CTRL_OUTPUT_HANDSAKE) || (c2_ctrl == VIA_C2_CTRL_OUTPUT_PULSE));
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

static void via_set_ifr(via_t handle, uint8_t bits)
{
    uint8_t cur_ifr7;
    uint8_t new_bits;

    if(bits == 0)
    {
        return;
    }

    cur_ifr7 = handle->ifr & 0x80;
    handle->ifr |= bits | 0x80;

    if((cur_ifr7 == 0) && (handle->voter != BUS_SIGNAL_INVALID_VOTER))
    {
        emu_bus_sig_vote(handle->emu, handle->voter, BUS_SIG_IRQ, true);
    }
}

static void via_clear_ifr(via_t handle, uint8_t bits)
{
    bool cur_voted = handle->ifr != 0;

    handle->ifr &= ~(bits & 0x7f);

    if((handle->ifr & 0x7f) == 0)
    {
        handle->ifr = 0;

        if((cur_voted) && (handle->voter != BUS_SIGNAL_INVALID_VOTER))
        {
            emu_bus_sig_vote(handle->emu, handle->voter, BUS_SIG_IRQ, false);
        }
    }
}

static void via_write_c2_state(via_t handle, bool porta, bool state)
{
    via_port_state_t *port = porta ? &handle->porta : &handle->portb;
    uint8_t c2_ctrl = porta ? handle->pcr.bits.ca2_ctrl : handle->pcr.bits.cb2_ctrl;
    via_event_data_t event;

    if(state == port->c2_out)
    {
        /* Don't make any notifications if it didn't change. */
        return;
    }

    port->c2_out = state;

    /* If C2 is currently an output, trigger a callback. */
    if(c2_ctrl >= VIA_C2_CTRL_OUTPUT_HANDSAKE)
    {
        event.type = VIA_EV_PORT_CHANGE;
        event.data.port = VIA_CTRL;

        via_make_callbacks(handle, &event);
    }
}

static void via_handle_c1_edge(via_t handle, bool porta)
{
    via_port_state_t *port = porta ? &handle->porta : &handle->portb;
    bool latch = porta ? handle->acr.bits.pa_latch : handle->acr.bits.pb_latch;
    uint8_t c1_ctrl = porta ? handle->pcr.bits.ca1_ctrl : handle->pcr.bits.cb1_ctrl;
    uint8_t c2_ctrl = porta ? handle->pcr.bits.ca2_ctrl : handle->pcr.bits.cb2_ctrl;
    bool posedge = c1_ctrl != 0;

    /* Check if the configured edge occured on CX1. We know *an* edge occurred, so
     * we can just compare the current pin state here. */
    if(port->c1_in == posedge)
    {
        /* Handle latching if enabled. */
        if(latch)
        {
            /* Latch the current pin state into ir for inputs. */
            port->ir = port->pin_in;

        }

        /* TODO: HW3: It's unclear when all this happens, but does need
         * to happen for handshaking regardless of latch state it seems. */
        via_set_ifr(handle, porta ? IFR_CA1 : IFR_CB1);

        /* If CA2 is currently in handshake mode, make sure we clear the Data Taken
         * state now that new data is ready. */
        if(c2_ctrl == VIA_C2_CTRL_OUTPUT_HANDSAKE)
        {
            via_write_c2_state(handle, porta, true);
        }
    }

}

static void via_clock_tick(clk_t clk, clock_edge_t edge, void *userdata)
{
    via_event_data_t event;
    via_t handle = (via_t)userdata;

    if(handle == NULL)
    {
        return;
    }

    if(edge == CLOCK_POSEDGE)
    {
        if(handle->flags & VIA_FLAG_CA2_PULSE_PEND)
        {
            via_write_c2_state(handle, true, true);
            handle->flags &= ~VIA_FLAG_CA2_PULSE_PEND;
        }

        if(handle->flags & VIA_FLAG_CB2_PULSE_PEND)
        {
            via_write_c2_state(handle, false, true);
            handle->flags &= ~VIA_FLAG_CB2_PULSE_PEND;
        }

        if(handle->flags & VIA_FLAG_CA2_TRIG_PEND)
        {
            via_write_c2_state(handle, true, false);
            handle->flags &= ~VIA_FLAG_CA2_TRIG_PEND;

            if(handle->pcr.bits.ca2_ctrl == VIA_C2_CTRL_OUTPUT_PULSE)
            {
                handle->flags |= VIA_FLAG_CA2_PULSE_PEND;
            }
        }

        if(handle->flags & VIA_FLAG_CB2_TRIG_PEND)
        {
            via_write_c2_state(handle, false, false);
            handle->flags &= ~VIA_FLAG_CB2_TRIG_PEND;

            if(handle->pcr.bits.cb2_ctrl == VIA_C2_CTRL_OUTPUT_PULSE)
            {
                handle->flags |= VIA_FLAG_CB2_PULSE_PEND;
            }
        }

        /* IFR/PB7 control of timers appears to happen on positive edge. */
        /* TODO: HW10. */
        if(VIA_CHECK_FLAG(handle, VIA_FLAG_T1_ARMED))
        {
            if(handle->t1c == 0xffff)
            {
                via_set_ifr(handle, IFR_T1);

                /* Just invert PB7. It doesnt matter here mode. */
                handle->t1pb7 = !handle->t1pb7;

                /* Inform callbacks of PB change if PB7 ourput is enabled. */
                if(handle->acr.bits.t1_pb7 == VIA_ACR_T1_PB7_ENABLED)
                {
                    event.type = VIA_EV_PORT_CHANGE;
                    event.data.port = VIA_PORTB;
                    via_make_callbacks(handle, &event);
                }

                if(handle->acr.bits.t1_mode == VIA_ACR_T1_MODE_ONE_SHOT)
                {
                    /* One shot timer, so deactivate it. */
                    VIA_CLEAR_FLAG(handle, VIA_FLAG_T1_ARMED);
                }
            }
        }
    }
    else
    {
        /* Read handshake pulsing happens at negedge. */
        if(VIA_CHECK_FLAG(handle, VIA_FLAG_CA2_READ_PULSE_PEND))
        {
            /* Note that due to implementation details of the emulator API, we get the
             * bus read prior to the corresponding clock negedge. We already initiated the
             * pulse on bus read, but we don't want to immediately kill it on this simultaneous
             * edge. This extra flag prevents that, so now clear it. */
            if(VIA_CHECK_FLAG(handle, VIA_FLAG_CA2_READ_TRIG_PEND))
            {
                VIA_CLEAR_FLAG(handle, VIA_FLAG_CA2_READ_TRIG_PEND);
            }
            else
            {
                via_write_c2_state(handle, true, true);
                VIA_CLEAR_FLAG(handle, VIA_FLAG_CA2_READ_PULSE_PEND);
            }
        }

        /* TODO: HW9 */

        /* Skip timer decrement if this is a negedge associated with the bus write. */
        if(!VIA_CHECK_FLAG(handle, VIA_FLAG_T1_WRITE_PEND))
        {
            /* Timer counter decrement happens at negedge. Note,
             * for T1 the implication from HW9 is that every edge
             * at 0xFFFF T1L->T1C loading occurs regardless of state. */
            if(handle->t1c == 0xffff)
            {
                handle->t1c = handle->t1l;
            }
            else
            {
                handle->t1c--;
            }
        }
        else
        {
            VIA_CLEAR_FLAG(handle, VIA_FLAG_T1_WRITE_PEND);
        }
    }
}

via_t via_init(const cbemu_t emu)
{
    via_t cxt;

    cxt = malloc(sizeof(struct via_s));

    if(cxt == NULL)
        return NULL;

    memset(cxt, 0, sizeof(struct via_s));
    list_init(&cxt->callbacks);
    cxt->voter = BUS_SIGNAL_INVALID_VOTER;

    /* HW11 */
    cxt->t1pb7 = true;

    if(emu != NULL)
    {
        cxt->emu = emu;
        cxt->clk_cb = clock_register_tick_edges(clock_get_core_clk(emu), via_clock_tick, (CLOCK_NEGEDGE | CLOCK_POSEDGE), cxt);

        if(cxt->clk_cb == NULL)
        {
            free(cxt);
            cxt = NULL;
        }
    }

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

    if(via->clk_cb != NULL)
    {
        clock_unregister_tick(via->clk_cb);
    }

    free(via);
}

bool via_register(via_t handle, const bus_decode_params_t *decoder, uint16_t base, bool irq)
{
    bool error = false;

    if((handle == NULL) || (handle->emu == NULL) || (decoder == NULL) || (handle->bus_handle != NULL) || (handle->voter != BUS_SIGNAL_INVALID_VOTER))
    {
        return false;
    }

    handle->bus_handle = emu_bus_register(handle->emu, decoder, &via_bus_handlers, handle);

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
                emu_bus_unregister(handle->emu, handle->bus_handle);
            }
        }
    }

    return !error;
}

void via_write(via_t handle, uint8_t reg, uint8_t val)
{
    acr_t old_acr;
    uint8_t ifr_bits;
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

            ifr_bits = IFR_CA1;
            ifr_bits |= via_is_independent_int(handle, true) ? IFR_CA2 : 0;
            via_clear_ifr(handle, ifr_bits);

            if(via_is_handshake_output(handle, true))
            {
                /* Trigger write handshake. */
                handle->flags |= VIA_FLAG_CA2_TRIG_PEND;
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
                dispatch = true;
                event.data.port = VIA_PORTB;
            }

            handle->portb.or = val;
            ifr_bits = IFR_CB1;
            ifr_bits |= via_is_independent_int(handle, false) ? IFR_CB2 : 0;
            via_clear_ifr(handle, ifr_bits);

            if(via_is_handshake_output(handle, false))
            {
                /* Trigger write handshake. */
                handle->flags |= VIA_FLAG_CB2_TRIG_PEND;
            }
            break;
        case T1CL:
            handle->t1l = (handle->t1l & 0xff00) | val;
            break;
        case T1CH:
            handle->t1l = (handle->t1l & 0x00ff) | ((uint16_t)val << 8);
            handle->t1c = handle->t1l;
            via_clear_ifr(handle, IFR_T1);

            /* TODO: HW11#4. For now, just always set PB7 low on first arming.
             *       Note that as far as I can tell re-arming the timer before it
             *       expires does *not* change PB7 (HW11#5). */
            if(!VIA_CHECK_FLAG(handle, VIA_FLAG_T1_ARMED))
            {
                handle->t1pb7 = false;
                event.data.port = VIA_PORTB;
                dispatch = true;
            }

            VIA_SET_FLAG(handle, (VIA_FLAG_T1_ARMED | VIA_FLAG_T1_WRITE_PEND));
            break;
        case T1LL:
            handle->t1l = (handle->t1l & 0xff00) | val;
            break;
        case T1LH:
            handle->t1l = (handle->t1l & 0x00ff) | ((uint16_t)val << 8);
            via_clear_ifr(handle, IFR_T1);
            break;
        case ACR:
            /* Skip handling if value is not changing to prevent unnecessary callbacks. */
            old_acr = handle->acr;
            handle->acr.val = val;

            /* Handle T1 PB7 setting change. */
            if(old_acr.bits.t1_pb7 != handle->acr.bits.t1_pb7)
            {
                /* Go ahead and make a callback that PB7 may have change. */
                event.data.port = VIA_PORTB;
                dispatch = true;
            }
            break;
        case PCR:
            handle->pcr.val = val;

            switch(handle->pcr.bits.ca2_ctrl)
            {
                case VIA_C2_CTRL_OUTPUT_HANDSAKE:
                case VIA_C2_CTRL_OUTPUT_PULSE:
                case VIA_C2_CTRL_OUTPUT_HIGH:
                    /* TODO: HW5 */
                    via_write_c2_state(handle, true, true);
                    break;
                case VIA_C2_CTRL_OUTPUT_LOW:
                    via_write_c2_state(handle, true, false);
                default:
                    /* TODO: HW5 */
                    break;
            }

            switch(handle->pcr.bits.cb2_ctrl)
            {
                case VIA_C2_CTRL_OUTPUT_HANDSAKE:
                case VIA_C2_CTRL_OUTPUT_PULSE:
                case VIA_C2_CTRL_OUTPUT_HIGH:
                    /* TODO: HW5 */
                    via_write_c2_state(handle, false, true);
                    break;
                case VIA_C2_CTRL_OUTPUT_LOW:
                    via_write_c2_state(handle, false, false);
                default:
                    /* TODO: HW5 */
                    break;
            }
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
            break;
        case IFR:
            via_clear_ifr(handle, val & 0x7f);
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
            return handle->porta.ddr;
        case DDRB:
            return handle->portb.ddr;

        case DATAA:
            via_clear_ifr(handle, IFR_CA1);

            /* If CA2 is in handshake mode, generated the Data Taken signal
             * for read handshaking. */
            if(via_is_handshake_output(handle, true))
            {
                /* Read handshaking is triggered on read transaction as opposed
                 * to write handshaking which happens on the next half phi2 cycle. */
                via_write_c2_state(handle, true, false);

                if(handle->pcr.bits.ca2_ctrl == VIA_C2_CTRL_OUTPUT_PULSE)
                {
                    /* If pulse output, flag for the pulse to end on the next
                     * falling clock edge. */
                    VIA_SET_FLAG(handle, (VIA_FLAG_CA2_READ_TRIG_PEND | VIA_FLAG_CA2_READ_PULSE_PEND));
                }
            }

            /* In reality, PORTA is a bit special in that output pins always read back
             * the physical pin state rather than the current output register state. In
             * practice, this is difficult/not very meaningful to emulate, so we won't. */
            return MERGE_BITS(handle->porta.or, handle->porta.ir, handle->porta.ddr);
        case DATAB:
            via_clear_ifr(handle, IFR_CB1);
            return MERGE_BITS(handle->portb.or, handle->portb.ir, handle->portb.ddr);
        case T1CL:
            via_clear_ifr(handle, IFR_T1);
            return (handle->t1c & 0xFF);
        case T1CH:
            return (handle->t1c >> 8);
        case T1LL:
            return (handle->t1l & 0xFF);
        case T1LH:
            /* TODO: HW8 */
            return (handle->t1l >> 8);
        case ACR:
            return handle->acr.val;
        case PCR:
            return handle->pcr.val;
        case IER:
            return handle->ier;
        case IFR:
            return handle->ifr;
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
    uint8_t val;
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
        val = MERGE_BITS(handle->portb.or, handle->portb.pin_in, handle->portb.ddr);

        /* If PB7 output for T1 is enabled, override PB7 with the internal T1 state (HW11) */
        if(handle->acr.bits.t1_pb7 == VIA_ACR_T1_PB7_ENABLED)
        {
            val &= 0x7F;

            if(handle->t1pb7)
            {
                val |= 0x80;
            }
        }

        return val;
    }
}

bool via_read_ctrl(via_t handle, via_ctrl_pin_t pin)
{
    via_port_state_t *port;
    uint8_t c2_ctrl;

    if(handle == NULL)
    {
        return false;
    }

    switch(pin)
    {
        case VIA_CA1:
            return handle->porta.c1_in;
        case VIA_CB1:
            return handle->portb.c1_in;
        case VIA_CA2:
        case VIA_CB2:
            port = (pin == VIA_CA2) ? &handle->porta : &handle->portb;
            c2_ctrl = (pin == VIA_CA2) ? handle->pcr.bits.ca2_ctrl : handle->pcr.bits.cb2_ctrl;

            if(c2_ctrl < VIA_C2_CTRL_OUTPUT_HANDSAKE)
            {
                return port->c2_in;
            }
            else
            {
                return port->c2_out;
            }
            break;
        default:
            return false;
    }
}
