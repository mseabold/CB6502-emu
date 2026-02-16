#include <stdlib.h>

#include "emu_priv_types.h"
#include "bus.h"
#include "log.h"

#define MAX_SIG_VOTERS  (sizeof(bus_signal_voter_t) * 8)

/** Tracking structure for a bus connection. */
typedef struct bus_conn_s
{
    listnode_t list;            /**< List node */
    bus_decode_params_t params; /**< Address decode parameters for the connection */
    bus_handlers_t handlers;    /**< Memory operation handler callbacks */
    void *userdata;             /**< User parameter for callbacks */
} bus_conn_t;

/** Tracking structure for a bus tracer */
typedef struct bus_tracer_s
{
    listnode_t list;            /**< list node */
    bus_trace_cb_t callback;    /**< Callback function */
    void *userdata;             /**< User parameter for callback */
} bus_tracer_t;

static bool bus_match_addr(bus_decode_params_t *params, uint16_t addr, bool write, void *userdata);
static bool bus_validate_params(const bus_decode_params_t *params);
static uint8_t bus_read_peek_i(bus_t *bus, uint16_t addr, bool peek, bool sync);

/**
 * Determines if a given bus connection parameters matches a given address
 *
 * @param[in] params    The bus connection parameters to check
 * @param[in] addr      The address to check for a match
 * @param[in] write     Indicates of the requested operation is a read or a write
 * @param[in] userdata  The registered callback parameter to make to any custom decode callback
 *
 * @return true of the given address matches the decoder parameters
 */
static bool bus_match_addr(bus_decode_params_t *params, uint16_t addr, bool write, void *userdata)
{
    bool match;

    switch(params->type)
    {
        case BUSDECODE_RANGE:
            match = (addr >= params->value.range.addr_start) && (addr <= params->value.range.addr_end);
            break;
        case BUSDECODE_MASK:
            match = ((addr & params->value.mask.addr_mask) == params->value.mask.addr_value);
            break;
        case BUSDECODE_CUSTOM:
            match = params->value.custom(addr, write, userdata);
            break;
        default:
            match = false;
            break;
    }

    return match;
}

/**
 * Validates bus decode parameters to ensure that they are valid
 *
 * @param[in] params    The bus parameters to validate
 *
 * @return true if the parameters are valid
 */
static bool bus_validate_params(const bus_decode_params_t *params)
{
    bool valid;

    switch(params->type)
    {
        case BUSDECODE_RANGE:
            valid = params->value.range.addr_end >= params->value.range.addr_start;
            break;
        case BUSDECODE_MASK:
            valid = params->value.mask.addr_mask != 0;
            break;
        case BUSDECODE_CUSTOM:
            valid = true;
            break;
        default:
            valid = false;
            break;
    }

    return valid;
}

/**
 * Internal helper for handling both read and peek operations
 *
 * @param[in] bus   The bus instance
 * @param[in] addr  Address to read or peek
 * @param[in] peek  Indicates whether this is read or peek
 * @param[in] sync  Indicates whether this is a sync (opcode) read
 *
 * @return The result of the read or peek operation
 */
static uint8_t bus_read_peek_i(bus_t *bus, uint16_t addr, bool peek, bool sync)
{
    uint8_t ret;
    uint8_t conn_read_val;
    bool matched = false;
    listnode_t *cur;
    bus_conn_t *conn;
    bus_tracer_t *tracer;
    bus_read_cb_t cb;

    list_iterate(&bus->connlist, cur)
    {
        conn = list_container(cur, bus_conn_t, list);

        if(bus_match_addr(&conn->params, addr, true, conn->userdata))
        {
            cb = peek ? conn->handlers.peek : conn->handlers.read;

            if(cb)
            {
                conn_read_val = cb(addr, sync ? SYNC : 0, conn->userdata);

                if((matched) && (conn_read_val != ret))
                {
                    log_print(lWARNING, "Multiple bus connections driving data for Address: 0x%04x. This could damage actual HW.", addr);
                }
                else
                {
                    matched = true;
                    ret = conn_read_val;
                }
            }
        }
    }

    if(!matched)
    {
        ret = 0xFF;
    }

    /* only trace on actual bus transactions */
    if(!peek)
    {
        list_iterate(&bus->tracelist, cur)
        {
            tracer = list_container(cur, bus_tracer_t, list);

            tracer->callback(addr, ret, false, sync ? SYNC : 0, tracer->userdata);
        }

        /* Only log the last operation if it is committed. */
        bus->lastop.write = false;
        bus->lastop.addr = addr;
        bus->lastop.flags = sync ? SYNC : 0;
    }

    return ret;
}

/**
 * Internal function to initialize a bus instance
 *
 * @param[in] emu   Emulator context
 *
 * @return true if initialization is successful
 */
bool bus_init(cbemu_t emu)
{
    bus_t *bus = &emu->bus;

    list_init(&bus->connlist);
    list_init(&bus->tracelist);
    bus->init = true;

    return true;
}

/**
 * Internal function to clean up a bus instance
 *
 * @param[in] emu   Emulator context
 */
void bus_cleanup(cbemu_t emu)
{
    bus_t *bus = &emu->bus;
    bus_conn_t *conn;
    bus_tracer_t *tracer;
    listnode_t *tail;

    if(!bus->init)
    {
        return;
    }

    while(!list_empty(&bus->connlist))
    {
        tail = list_tail(&bus->connlist);

        list_remove(tail);

        conn = list_container(tail, bus_conn_t, list);

        free(conn);
    }

    while(!list_empty(&bus->tracelist))
    {
        tail = list_tail(&bus->tracelist);

        list_remove(tail);

        tracer = list_container(tail, bus_tracer_t, list);

        free(tracer);
    }
}

/**
 * Internal bus access function to perform a bus read operation. This will attempt
 * to decode the address and perform a read operation with any registered bus connection.
 * This is a committed read operation, i.e. what would occur on the falling edge of PHI2.
 *
 * @param[in] emu   Emulator context
 * @param[in] addr  Address to read on the bus
 *
 * @return The bus value returned at the given address
 */
uint8_t bus_read(cbemu_t emu, uint16_t addr)
{
    bus_t *bus = &emu->bus;

    return bus_read_peek_i(bus, addr, false, false);
}

/**
 * Internal bus access function to perform a bus read operation. This will attempt
 * to decode the address and perform a read operation with any registered bus connection.
 * This is a committed read operation, i.e. what would occur on the falling edge of PHI2.
 * This should only be used for reads that would occur that also assert the SYNC pin, which
 * is during opcode fetch.
 *
 * @param[in] emu   Emulator context
 * @param[in] addr  Address to read on the bus
 *
 * @return The bus value returned at the given address
 */
uint8_t bus_sync_read(cbemu_t emu, uint16_t addr)
{
    bus_t *bus = &emu->bus;

    return bus_read_peek_i(bus, addr, false, true);
}

/**
 * Internal bus access function to read from a given address without actually committing a read
 * operation (PHI2 tick). This is can be useful for debugging or poking the state of the system
 * without triggering any read-triggered events (such as auto-clearing of interrupt or status
 * bits on read).
 *
 * @param[in] emu   Emulator context
 * @param[in] addr  Address to read on the bus
 *
 * @return The bus value returned at the given address
 */
uint8_t bus_peek(cbemu_t emu, uint16_t addr)
{
    bus_t *bus = &emu->bus;

    return bus_read_peek_i(bus, addr, true, false);
}

/**
 * Internal bus access function to perfrom a write operation. This will attempt
 * to decode the address and write the given value to the appropriate bus connection.
 *
 * @param[in] emu   Emulator context
 * @param[in] addr  Address to write on the bus
 * @param[in] value Value to write to the given address
 */
void bus_write(cbemu_t emu, uint16_t addr, uint8_t value)
{
    bus_t *bus = &emu->bus;
    listnode_t *cur;
    bus_conn_t *conn;
    bus_tracer_t *tracer;
    bus_read_cb_t cb;

    list_iterate(&bus->connlist, cur)
    {
        conn = list_container(cur, bus_conn_t, list);

        if(bus_match_addr(&conn->params, addr, false, conn->userdata))
        {
            if(conn->handlers.write)
            {
                conn->handlers.write(addr, value, 0, conn->userdata);
            }
        }
    }

    list_iterate(&bus->tracelist, cur)
    {
        tracer = list_container(cur, bus_tracer_t, list);

        tracer->callback(addr, value, true, 0, tracer->userdata);
    }

    emu->bus.lastop.write = true;
    emu->bus.lastop.addr = addr;
    emu->bus.lastop.val = value;
    emu->bus.lastop.flags = 0;
}

/**
 * Replays the last bus operation. If it was a read, the result is discarded.
 *
 * @param[in] emu   Emulator context
 */
void bus_replay(cbemu_t emu)
{
    if(emu->bus.lastop.write)
    {
        bus_write(emu, emu->bus.lastop.addr, emu->bus.lastop.val);
    }
    else
    {
        (void)bus_read_peek_i(&emu->bus, emu->bus.lastop.addr, false, (emu->bus.lastop.flags & SYNC) ? true : false);
    }
}

/**
 * Registers a bus connection with a set of handler callbacks with the specific decoder parameters
 *
 * @param[in] emu       The emulator core
 * @param[in] params    Address decoding parameters for the bus connection
 * @param[in] handlers  Handler functions called when the bus address matches the decoding
 * @param[in] userdata  App-Specific userdata provided to the callback when made
 *
 * @return A handle for the registered connection or NULL on error
 */
bus_cb_handle_t emu_bus_register(cbemu_t emu, const bus_decode_params_t *params, const bus_handlers_t *handlers, void *userdata)
{
    bus_conn_t *conn;

    if(emu == NULL || params == NULL || handlers == NULL || !bus_validate_params(params))
    {
        return NULL;
    }

    conn = malloc(sizeof(bus_conn_t));

    if(conn != NULL)
    {
        conn->params = *params;
        conn->handlers = *handlers;
        conn->userdata = userdata;

        list_add_tail(&emu->bus.connlist, &conn->list);
    }

    return conn;
}

/**
 * Un-registers a previously registered bus connection
 *
 * @param[in] emu       The emulator core
 * @param[in] handle    The registered bus handle
 */
void emu_bus_unregister(cbemu_t emu, bus_cb_handle_t handle)
{
    bus_conn_t *conn = (bus_conn_t *)handle;

    if(conn != NULL)
    {
        /* TODO For safety, should maybe call list_contains. */
        list_remove(&conn->list);

        free(conn);
    }
}

/**
 * Adds a bus tracer callback to the bus. This is called on every bus transaction to allow
 * debugging, testing, or monitoring of bus activity.
 *
 * @param[in] emu       The emulator core
 * @param[in] callback  Tracer callback to be called for each bus transaction
 * @param[in] userdata  App-specific user data provided to the callback when made
 *
 * @return A handle for the registered callback or NULL on error
 */
bus_cb_handle_t emu_bus_add_tracer(cbemu_t emu, const bus_trace_cb_t callback, void *userdata)
{
    bus_tracer_t *tracer;

    if(emu == NULL || callback == NULL)
    {
        return NULL;
    }

    tracer = malloc(sizeof(bus_tracer_t));

    if(tracer != NULL)
    {
        tracer->callback = callback;
        tracer->userdata = userdata;

        list_add_tail(&emu->bus.tracelist, &tracer->list);
    }

    return tracer;
}

/**
 * Removes a previously registered tracer callback
 *
 * @param[in] emu       The emulator core
 * @oaram[in] handle    Handle of the previously registered callback
 */
void emu_bus_remove_tracer(cbemu_t emu, bus_cb_handle_t handle)
{
    bus_tracer_t *tracer = (bus_tracer_t *)handle;

    if(tracer != NULL)
    {
        list_remove(&tracer->list);

        free(tracer);
    }
}

/**
 * Registers a bus signal voter with the emulator core
 *
 * @return Returns the voter handle to be used when making signal votes
 *         or BUS_SIGNAL_INVALID_VOTER if no voters are available.
 */
bus_signal_voter_t emu_bus_register_sig_voter(cbemu_t emu)
{
    bus_signal_voter_t voter = BUS_SIGNAL_INVALID_VOTER;
    bus_signal_voter_t test_mask;
    uint8_t index;

    if(emu == NULL)
    {
        return voter;
    }

    for(index = 0; index < MAX_SIG_VOTERS; index++)
    {
        test_mask = (1 << index);
        if((emu->bus.sigvotes.allocated & test_mask) == 0)
        {
            emu->bus.sigvotes.allocated |= test_mask;
            voter = test_mask;
            break;
        }
    }

    return voter;
}

/**
 * Releases a bus signal voter handle
 *
 * @param[in] voter     The previously registered handle.
 */
void emu_bus_unregister_sig_voter(cbemu_t emu, bus_signal_voter_t voter)
{
    if(emu == NULL)
    {
        return;
    }

    emu->bus.sigvotes.allocated &= ~voter;
}

/**
 * Takes/releases a vote on a given signal for the specified voter.
 *
 * @param[in] voter     The previously registered voter handle
 * @param[in] signal    The signal to vote/unvote for
 * @param[in] voted     Indicates whether to take or release the vote on the signal
 */
void emu_bus_sig_vote(cbemu_t emu, bus_signal_voter_t voter, bus_signal_t signal, bool voted)
{
    bus_signal_voter_t *mask;
    bus_sigvotes_t *sigvotes;

    if((emu == NULL) || ((emu->bus.sigvotes.allocated & voter) == 0))
    {
        return;
    }

    sigvotes = &emu->bus.sigvotes;

    switch(signal)
    {
        case BUS_SIG_IRQ:
            mask = &sigvotes->irq;
            break;
        case BUS_SIG_NMI:
            mask = &sigvotes->nmi;

            if(voted && (sigvotes->nmi == 0))
            {
                /* If this is the first vote for NMI, flag the rising edge. CPU core only detects the
                 * edge of NMI. */
                sigvotes->flags |= SV_NMI_EDGE_PENDING;
            }
            break;
        case BUS_SIG_RDY:
            mask = &sigvotes->rdy;
            break;
        case BUS_SIG_BE:
            mask = &sigvotes->be;
            break;
        default:
            /* Signal not known, so do not process it. */
            return;
    }

    if(voted)
    {
        *mask |= voter;
    }
    else
    {
        *mask &= ~voter;
    }
}
