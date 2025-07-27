#ifndef __BUS_PRIV_H__
#define __BUS_PRIV_H__

#include "emu_priv_types.h"

/**
 * Internal function to initialize a bus instance
 *
 * @param[in] emu   Emulator context
 *
 * @return true if initialization is successful
 */
bool bus_init(cbemu_t emu);

/**
 * Internal function to clean up a bus instance
 *
 * @param[in] emu   Emulator context
 */
void bus_cleanup(cbemu_t emu);

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
uint8_t bus_read(cbemu_t emu, uint16_t addr);

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
uint8_t bus_peek(cbemu_t emu, uint16_t addr);

/**
 * Internal bus access function to perfrom a write operation. This will attempt
 * to decode the address and write the given value to the appropriate bus connection.
 *
 * @param[in] emu   Emulator context
 * @param[in] addr  Address to write on the bus
 * @param[in] value Value to write to the given address
 */
void bus_write(cbemu_t emu, uint16_t addr, uint8_t value);

#endif /* end of include guard: __BUS_PRIV_H__ */
