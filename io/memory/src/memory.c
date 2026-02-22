#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "log.h"

struct memory_s
{
    memory_flags_t flags;
    uint16_t size;
    uint16_t base;
    cbemu_t emulator;
    bus_cb_handle_t bus_handle;
    uint8_t buffer[];
};

static void mem_bus_write_cb(uint16_t addr, uint8_t value, bus_flags_t flags, void *userdata)
{
    memory_t handle = (memory_t)userdata;
    uint16_t internal_addr;

    if(handle == NULL)
    {
        return;
    }

    if(handle->flags & MEMFLAG_ROM)
    {
        log_print(lWARNING, "Attempt to write to ROM memory at addres 0x%04x", addr);
        return;
    }

    internal_addr = addr - handle->base;

    if(internal_addr >= handle->size)
    {
        log_print(lWARNING, "Attempted write to decoded address outside memory bounds: 0x%04x", addr);
        return;
    }

    handle->buffer[internal_addr] = value;
}

static uint8_t mem_bus_read_cb(uint16_t addr, bus_flags_t flags, void *userdata)
{
    memory_t handle = (memory_t)userdata;
    uint16_t internal_addr;

    if(handle == NULL)
    {
        return 0xFF;
    }

    internal_addr = addr - handle->base;

    if(internal_addr >= handle->size)
    {
        log_print(lWARNING, "Attempted read from decoded address outside memory bounds: 0x%04x", addr);
        return 0xFF;
    }

    return handle->buffer[internal_addr];
}

static const bus_handlers_t mem_bus_handlers =
{
    mem_bus_write_cb,
    mem_bus_read_cb,
    mem_bus_read_cb
};

/**
 * Initializes a memory instance.
 *
 * @param[in] size          The size of the memory to allocate.
 * @param[in] flags         Boolean flags for instance configuration. See memory_flags_t.
 * @param[in] bus_params    If non-NULL, will be used to register a bus handler with
 *                          with the supplied emulator instance. If decoding is to
 *                          be done externally from the memory instance, then this can be
 *                          NULL. In such case, the caller can performing any bus decoding
 *                          and call memory_read/memory_write directly.
 */
memory_t memory_init(uint16_t size, memory_flags_t flags, const io_bus_params_t *bus_params)
{
    memory_t handle;

    if((bus_params != NULL) && ((bus_params->emulator == NULL) || (bus_params->decoder != NULL)))
    {
        return NULL;
    }

    handle = malloc(sizeof(struct memory_s) + size);

    if(handle != NULL)
    {
        memset(handle, 0, sizeof(struct memory_s) + size);

        if(bus_params != NULL)
        {
            handle->bus_handle = emu_bus_register(bus_params->emulator, bus_params->decoder, &mem_bus_handlers, handle);
            handle->emulator = bus_params->emulator;
            handle->base = bus_params->base;
        }

        if((bus_params == NULL) || (handle->bus_handle != NULL))
        {
            handle->size = size;
            handle->flags = flags;
        }
        else
        {
            free(handle);
            handle = NULL;
        }
    }

    return handle;
}

/**
 * Destorys a memory instance
 *
 * @param[in] memory    The instance to destroy.
 */
void memory_cleanup(memory_t memory)
{
    if(memory == NULL)
    {
        return;
    }

    if(memory->emulator != NULL && memory->bus_handle != NULL)
    {
        emu_bus_unregister(memory->emulator, memory->bus_handle);
    }

    free(memory);
}

/**
 * Reads a single byte from a memory instance.
 *
 * @param[in] memory    The memory instance
 * @param[in] addr      The address to read from. Note that this is the internal address/offset
 *                      of the underlying memory intance. It does not perform any address decoding.
 *
 * @return The byte at the given memory address.
 */
uint8_t memory_read(memory_t memory, uint16_t addr)
{
    if((memory != NULL) && (addr < memory->size))
    {
        return memory->buffer[addr];
    }

    return 0xFF;
}

/**
 * Writes a single byte to a memory instance.
 *
 * @param[in] memory    The memory instance
 * @param[in] addr      The address to write to. Note that this is the internal address/offset
 *                      of the underlying memory intance. It does not perform any address decoding.
 * @param[in] value     The byte to write to the memory.
 */
void memory_write(memory_t memory, uint16_t addr, uint8_t value)
{
    if((memory != NULL) && (addr < memory->size))
    {
        memory->buffer[addr] = value;
    }
}

/**
 * Reads/dumps an entire section of the memory into a supplied buffer.
 *
 * @param[in] memory    The memory instance.
 * @param[in/out] size  On input, should contain the size of the given buffer. Will be
 *                      populated with the size of the data actually copied on output.
 * @param[in] buffer    The buffer to dump the memory to.
 */
void memory_dump(memory_t memory, uint16_t *size, uint8_t *buffer)
{
    uint16_t copy_size;

    if((memory == NULL) || (size == NULL) || (buffer == NULL) || (*size == 0))
    {
        if(size != NULL)
        {
            *size = 0;
        }

        return;
    }

    copy_size = (*size < memory->size) ? *size : memory->size;

    memcpy(buffer, memory->buffer, copy_size);
    *size = copy_size;
}

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
void memory_load_data(memory_t memory, uint16_t data_size, uint8_t *data, uint16_t offset, bool use_fill, uint8_t fill_val)
{
    uint16_t copy_size;

    if((memory == NULL) || (data_size == 0) || (data == NULL) || (offset >= memory->size))
    {
        return;
    }

    copy_size = (data_size < memory->size) ? data_size : memory->size;

    copy_size -= offset;

    memcpy(&memory->buffer[offset], data, copy_size);

    if(use_fill)
    {
        if(offset > 0)
        {
            memset(memory->buffer, fill_val, offset);
        }

        if((offset + copy_size) < memory->size)
        {
            memset(&memory->buffer[offset+copy_size], fill_val, memory->size - (offset+copy_size));
        }
    }
}
