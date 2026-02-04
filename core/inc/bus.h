#ifndef __BUS_API_H__
#define __BUS_API_H__

#include "emu_types.h"

/** Type of bus decode parameters */
typedef enum
{
    BUSDECODE_RANGE,    /**< Address range */
    BUSDECODE_MASK,     /**< Specific address bit values */
    BUSDECODE_CUSTOM    /**< Custom callback-defined logic */
} bus_decode_type_t;

/** Parameters for Address Range bus decoding */
typedef struct
{
    /** Start address of decode range. This is inclusive. */
    uint16_t addr_start;

    /** End address of the decode range. THis is inclusive. */
    uint16_t addr_end;
} bus_decode_range_t;

/** Parameters for bitmask bus decoding */
typedef struct
{
    /** Bitmask of meaningful address bits to decode. 0b1 means the corresponding bit
     *  if the address being decoded should match the below supplied value. 0b0 means
     *  that corresponding bit is a Don't Care. */
    uint16_t addr_mask;

    /** Expected bits of the decoded address to match. */
    uint16_t addr_value;
} bus_decode_mask_t;

/** Callback prototype for custom bus decoding */
typedef bool (*bus_decode_cb_t)(uint16_t addr, bool write, void *userdata);

/** Bus decoding parameters */
typedef struct
{
    /** Type of decoding parameters contained */
    bus_decode_type_t type;

    /** Value of decoding parameters */
    union
    {
        bus_decode_range_t range; /**< Address Range parameters */
        bus_decode_mask_t mask;   /**< Address Bitmask parameters */
        bus_decode_cb_t custom;   /**< Custom callback */
    } value;
} bus_decode_params_t;

/** Handle for register bus callback */
typedef void *bus_cb_handle_t;

/** Enum defining the possible flags in a bus handler/tracer callback. */
typedef enum
{
    SYNC = 0x01, /**< Used during a read operation to denote the SYNC pin would be high on the 6502 bus. */
} bus_flags_t;

/** Defines the type of signals to the emulator/CPU from the bus that can be controlled externally. */
typedef enum
{
    BUS_SIG_IRQ, /**< Controls the IRQB signal on the 6502 core. */
    BUS_SIG_NMI, /**< Controls the NMIB signal on the 6502 core. */
    BUS_SIG_RDY, /**< Controls the RDY signal on the 6502 core. */
    BUS_SIG_BE, /**< Controls the Bus Enable signal on the 6502 core. */
} bus_signal_t;

/** Defines the handle type returned when registering a signal voter. */
typedef uint32_t bus_signal_voter_t;

#define BUS_SIGNAL_INVALID_VOTER (UINT32_MAX)

/**
 * Bus write callback.
 *
 * @param addr[in] Address to write
 * @param value[in] Value to write to the specified address
 * @param userdata[in] Userdata supplied by the callback owner
 */
typedef void (*bus_write_cb_t)(uint16_t addr, uint8_t value, bus_flags_t flags, void *userdata);

/**
 * Bus read callback
 *
 * @param addr[in] Address to read
 * @param userdata[in] Userdata supplied by the callback owner
 *
 * @return Value read at specified address
 */
typedef uint8_t (*bus_read_cb_t)(uint16_t addr, bus_flags_t flags, void *userdata);

/**
 * Bus trace debug callback. This can be registered to trace memory operations without actually
 * controlling the memory bus.
 *
 * @param addr[in]  Address for the memory option.
 * @param value[in] Data value that was written or read.
 * @param write[in] Indicates whether the operation is a read or write operation.
 * @param param[in] User-supplied param given when the callback was registered
 */
typedef void (*bus_trace_cb_t)(uint16_t addr, uint8_t value, bool write, bus_flags_t flags, void *param);

/** Container for bus callback functions */
typedef struct
{
    /** Called when a memory write is performed on the registered address. */
    bus_write_cb_t write;

    /** Called whne a memory read is performed on the registered address. */
    bus_read_cb_t read;

    /** Similar to read callback, but it not a committed bus cycle. This is can be
     *  used by a peripheral for which performing a read has some effect. This
     *  is called when the emulator wants to peek memory at address (such as for the
     *  debugger), but does *not* want to actually perform a read that may cause such
     *  a read-triggered action. */
    bus_read_cb_t peek;
} bus_handlers_t;

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
bus_cb_handle_t emu_bus_register(cbemu_t emu, const bus_decode_params_t *params, const bus_handlers_t *handlers, void *userdata);

/**
 * Un-registers a previously registered bus connection
 *
 * @param[in] emu       The emulator core
 * @param[in] handle    The registered bus handle
 */
void emu_bus_unregister(cbemu_t emu, bus_cb_handle_t handle);

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
bus_cb_handle_t emu_bus_add_tracer(cbemu_t emu, const bus_trace_cb_t callback, void *userdata);

/**
 * Removes a previously registered tracer callback
 *
 * @param[in] emu       The emulator core
 * @oaram[in] handle    Handle of the previously registered callback
 */
void emu_bus_remove_tracer(cbemu_t emu, bus_cb_handle_t handle);

/**
 * Registers a bus signal voter with the emulator core
 *
 * @param[in] emu       The emulator core
 *
 * @return Returns the voter handle to be used when making signal votes
 *         or BUS_SIGNAL_INVALID_VOTER if no voters are available.
 */
bus_signal_voter_t emu_bus_register_sig_voter(cbemu_t emu);

/**
 * Releases a bus signal voter handle
 *
 * @param[in] emu       The emulator core
 * @param[in] voter     The previously registered handle.
 */
void emu_bus_unregister_sig_voter(cbemu_t emu, bus_signal_voter_t voter);

/**
 * Takes/releases a vote on a given signal for the specified voter.
 *
 * @param[in] emu       The emulator core
 * @param[in] voter     The previously registered voter handle
 * @param[in] signal    The signal to vote/unvote for
 * @param[in] voted     Indicates whether to take or release the vote on the signal
 */
void emu_bus_sig_vote(cbemu_t emu, bus_signal_voter_t voter, bus_signal_t signal, bool voted);

#endif /* end of include guard: __BUS_API_H__ */
