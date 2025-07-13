#ifndef __BUS_API_H__
#define __BUS_API_H__

#include "emu_types.h"

typedef enum
{
    BUSDECODE_RANGE,
    BUSDECODE_MASK,
    BUSDECODE_CUSTOM
} bus_decode_type_t;

typedef struct
{
    /** Start address of decode range. This is inclusive. */
    uint16_t addr_start;

    /** End address of the decode range. THis is inclusive. */
    uint16_t addr_end;
} bus_decode_range_t;

typedef struct
{
    /** Bitmask of meaningful address bits to decode. 0b1 means the corresponding bit
     *  if the address being decoded should match the below supplied value. 0b0 means
     *  that corresponding bit is a Don't Care. */
    uint16_t addr_mask;

    /** Expected bits of the decoded address to match. */
    uint16_t addr_value;
} bus_decode_mask_t;

typedef bool (*bus_decode_cb_t)(uint16_t addr, bool write, void *userdata);

typedef struct
{
    bus_decode_type_t type;
    union
    {
        bus_decode_range_t range;
        bus_decode_mask_t mask;
        bus_decode_cb_t custom;
    } value;
} bus_decode_params_t;

typedef void *bus_cb_handle_t;

/**
 * Bus write callback.
 *
 * @param addr[in] Address to write
 * @param value[in] Value to write to the specified address
 * @param userdata[in] Userdata supplied by the callback owner
 */
typedef void (*bus_write_cb_t)(uint16_t addr, uint8_t value, void *userdata);

/**
 * Bus read callback
 *
 * @param addr[in] Address to read
 * @param userdata[in] Userdata supplied by the callback owner
 *
 * @return Value read at specified address
 */
typedef uint8_t (*bus_read_cb_t)(uint16_t addr, void *userdata);

/**
 * Bus trace debug callback. This can be registered to trace memory operations without actually
 * controlling the memory bus.
 *
 * @param addr[in]  Address for the memory option.
 * @param value[in] Data value that was written or read.
 * @param write[in] Indicates whether the operation is a read or write operation.
 * @param param[in] User-supplied param given when the callback was registered
 */
typedef void (*bus_trace_cb_t)(uint16_t addr, uint8_t value, bool write, void *param);

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

bus_cb_handle_t emu_bus_register(cbemu_t emu, const bus_decode_params_t *params, const bus_handlers_t *handlers, void *userdata);
void emu_bus_unregister(cbemu_t emu, bus_cb_handle_t handle);

bus_cb_handle_t emu_bus_add_tracer(cbemu_t emu, const bus_trace_cb_t callback, void *userdata);
void emu_bus_remove_tracer(cbemu_t emu, bus_cb_handle_t handle);

#endif /* end of include guard: __BUS_API_H__ */
