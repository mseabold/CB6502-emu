#ifndef __MEMORY_H__
#define __MEMORY_H__

#include "emulator.h"

typedef struct memory_s *memory_t;

/**
 * Possible flag values given when initializing a memory instance.
 */
typedef enum
{
    MEMFLAG_ROM = 0x01 /**< Indicates that the memory instnace is read-only. */
} memory_flags_t;

/**
 * Initializes a memory instance.
 *
 * @param[in] size          The size of the memory to allocate.
 * @param[in] flags         Boolean flags for instance configuration. See memory_flags_t.
 *
 * @return The handle if the new memory instance or NULL on error
 */
memory_t memory_init(uint16_t size, memory_flags_t flags);

/**
 * Registers the memory instance with the an emulator's bus.
 *
 * @param[in] memory    The memory instance to register.
 * @param[in] emu       The emulator to register the memory with.
 * @param[in] decoder   The decoder parameters to register with the bus.
 * @param[in] base_addr The base address if memory instnace within the memory space. This is used
 *                      to derive the local memory offset when an address is given from the bus.
 *                      This is purely a subtraction (bus_addr-base_addr). If a more complicateded
 *                      mapping protocol is needed, then external bus decoding should be used.
 *
 * @return true if the registration was successful.
 */
bool memory_register(memory_t memory, const cbemu_t emu, const bus_decode_params_t *decoder, uint16_t base_addr);

/**
 * Destorys a memory instance
 *
 * @param[in] memory    The instance to destroy.
 */
void memory_cleanup(memory_t memory);

/**
 * Reads a single byte from a memory instance.
 *
 * @param[in] memory    The memory instance
 * @param[in] addr      The address to read from. Note that this is the internal address/offset
 *                      of the underlying memory intance. It does not perform any address decoding.
 *
 * @return The byte at the given memory address.
 */
uint8_t memory_read(memory_t memory, uint16_t addr);

/**
 * Writes a single byte to a memory instance.
 *
 * @param[in] memory    The memory instance
 * @param[in] addr      The address to write to. Note that this is the internal address/offset
 *                      of the underlying memory intance. It does not perform any address decoding.
 * @param[in] value     The byte to write to the memory.
 */
void memory_write(memory_t memory, uint16_t addr, uint8_t value);

/**
 * Reads/dumps an entire section of the memory into a supplied buffer.
 *
 * @param[in] memory    The memory instance.
 * @param[in/out] size  On input, should contain the size of the given buffer. Will be
 *                      populated with the size of the data actually copied on output.
 * @param[in] buffer    The buffer to dump the memory to.
 */
void memory_dump(memory_t memory, uint16_t *size, uint8_t *buffer);

/**
 * Loads data into memory. This ignores the read-only flag, and can be used
 * to load a ROM image into the memory instance.
 *
 * @param[in] memory    The memory intance.
 * @param[in] data_size Size of the data to load into memory. Note if this is larger
 *                      than the available memory, it will be truncated.
 * @param[in] data      Buffer of data to load
 * @param[in] offset    Offset within the memory to load the data
 * @param[in] use_fill  Indicates whether to fill unused memory locations with fill_val
 * @param[in] fill_val  Value to fill unusued memory with if use_fill is indicated.
 */
void memory_load_data(memory_t memory, uint16_t data_size, uint8_t *data, uint16_t offset, bool use_fill, uint8_t fill_val);

#endif /* __MEMORY_H__ */
