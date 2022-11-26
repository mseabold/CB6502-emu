#ifndef __SYS_H__
#define __SYS_H__

#include <stdint.h>
#include <stdbool.h>

typedef struct sys_cxt_s *sys_cxt_t;
#define SYS_CXT_INVALID NULL

#define DEFAULT_TICKRATE_NS 1000

/**
 * Memory write callback.
 *
 * @param addr[in] Address to write
 * @param value[in] Value to write to the specified address
 */
typedef void (*mem_write_t)(uint16_t addr, uint8_t value);

/**
 * Memory read callback
 *
 * @param addr[in] Address to read
 *
 * @return Value read at specified address
 */
typedef uint8_t (*mem_read_t)(uint16_t addr);

typedef struct mem_space_s
{
    /**
     * Global memory write callback supplied by the platform. This function should
     * perform the correct address decoding and route the write to the correct module
     * based on the platform address space.
     */
    mem_write_t write;

    /**
     * Global memory read callback supplied by the platform. This function should
     * perform the correct address decoding and route the read to the correct module
     * based on the platform address space.
     */
    mem_read_t read;

    /**
     * This callback is used similarly to the global read callback with one key difference:
     * the read is not actually being executed by the CPU on the bus. This is used by debugging
     * or monitoring interfaces to peek the status of memory without potentially executing reads
     * that could modify the state of I/O devices which change on read transactions.
     */
    mem_read_t peek;
} mem_space_t;

/**
 * Initialized the a global emulator sys context. This context holds common
 * glue logic that connects all the emulators components such as bus read/read
 * and interrupt control. The platform implementation must provided bus handler to
 * perform address decoding and supply the read/writes to the proper components based on
 * the address space for the particular system.
 *
 * @param[in] read_hlr  The memory bus read handler to be provided by the platform
 * @param[in] write_hlr The memory bus write handler to be provided by the platform
 *
 * @return A sys context to be used for future calls, or NULL on failure to initialize.
 */
sys_cxt_t sys_init(mem_read_t read_hlr, mem_write_t write_hlr);

/**
 * Destroys a previously created global system context.
 *
 * @param[in] cxt The context to destroy
 */
void sys_destroy(sys_cxt_t cxt);

/**
 * Votes for a particular interrupt to be asserted or de-asserted. Any sys connected
 * emulator module can use this to request an interrupt from the system.
 *
 * Note that for every call with vote set to true, a subsequent call set to false
 * *must* occur. This includes if a module votes true multiple times. Multiple false
 * calls are required to release all votes taken. Modules should take care to either
 * only call once or keep track of how many outstanding votes it has taken.
 *
 * @param[in] cxt The sys context
 * @param[in] nmi Whether the vote is for an NMI or an IRQ interrupt
 * @param[in] vote Whether the vote is to assert (true) or de-assert (false) the interrupt. I.e.
 *                 A call to this function with vote set to true will cause the processor to
 *                 interrupt on the next instruction.
 */
void sys_vote_interrupt(sys_cxt_t cxt, bool nmi, bool vote);

/**
 * Used by the processor core to check if an interrupt is pending. Note that for
 * an NMI, this clears the pending interrupt, since it is edge triggered.
 *
 * @param[in] cxt The sys context
 * @param[in] nmi Whether to check for a pending NMI or IRQ interrupt
 *
 * @return true if the interrupt is pending.
 */
bool sys_check_interrupt(sys_cxt_t cxt, bool nmi);

/**
 * Perform a read on the system bus.
 *
 * @param[in] cxt The sys context
 * @param[in] addr The address to read
 *
 * @return The value associated with the address
 */
uint8_t sys_read_mem(sys_cxt_t cxt, uint16_t addr);

/**
 * Perform a write on the system bus.
 *
 * @param[in] cxt The sys context
 * @param[in] addr The address to write
 * @param[in] val The value to write to the specified address
 */
void sys_write_mem(sys_cxt_t cxt, uint16_t addr, uint8_t val);

/**
 * Set the tickrate (ns per CPU/Bus tick) for the system.
 *
 * @param[in] cxt The sys context
 * @param[in] tickrate The system tickrate
 */
void sys_set_tickrate(sys_cxt_t cxt, uint32_t tickrate);

/**
 * Convert a number of ticks into nanoseconds using the system tickrate.
 *
 * @param[in] cxt The sys context
 * @param[in] ticks The number of elapsed ticks
 *
 * @return The number of elapsed ns (rounded down).
 */
uint64_t sys_convert_ticks_to_ns(sys_cxt_t cxt, uint32_t ticks);

#endif
