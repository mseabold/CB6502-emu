/*
 * Copyright (c) 2020 Matt Seabold
 *
 * Interface for 6502 CPU simulator interface.
 *
 */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "mem.h"
#include "sys.h"

typedef enum
{
    REG_A,
    REG_X,
    REG_Y,
    REG_PC,
    REG_SP,
    REG_S
} cpu_reg_t;

typedef struct cpu_regs_s
{
    uint8_t a;
    uint8_t x;
    uint8_t y;
    uint8_t sp;
    uint16_t pc;
    uint8_t s;
} cpu_regs_t;

/**
 * Callback function to process elapsed CPU tick cycles.
 *
 * @param ticks Number of CPU ticks elapsed.
 */
typedef void (*cpu_tick_cb_t)(uint32_t ticks);

/**
 * Initialize the 6502 emulator. This sets the memory access functions as well as performs
 * an optional initial reset6502().
 *
 * @param system_cxt    Handle of the global system context. The CPU uses this to access the
 *                      memory space as well as check the status of the interrupt vectors.
 * @param reset         Perform a reset6502 as well as initialization.
 */
void cpu_init(sys_cxt_t system_cxt, bool reset);

/**
 * Resets the 6502 emulator and forces it to process the reset vector.
 */
void cpu_reset(void);

/**
 * Executes a single instruction
 *
 * @return The number of executed cycles.
 */
uint8_t cpu_step(void);

/**
 * Disassemble the current opcode into the supplied string buffer.
 *
 * @param buf_len   The length of the supplied buffer.
 * @param buffer    Buffer to hold the disassembly string.
 */
void cpu_disassemble(size_t buf_len, char *buffer);

/**
 * Disassemble the the opcode at the specified address into the supplied string buffer.
 *
 * @param addr      Address of the opcode to disassemble
 * @param buf_len   The length of the supplied buffer.
 * @param buffer    Buffer to hold the disassembly string.
 */
void cpu_disassemble_at(uint16_t addr, size_t buf_len, char *buffer);

/**
 * Get the interger value of a single CPU register.
 *
 * @param reg The CPU register to query.
 *
 * @return The value of the queried register.
 */
uint16_t cpu_get_reg(cpu_reg_t reg);

/**
 * Get the values of all the CPU regsiters.
 *
 * @param[out] regs     Pointer to a structure to hold all the register values.
 */
void cpu_get_regs(cpu_regs_t *regs);

/**
 * Get the length in bytes of the current opcode + parameters.
 *
 * @return The length of the opcode.
 */
uint8_t cpu_get_op_len(void);

/**
 * Get the length in bytes of the specified opcode + parameters.
 *
 * @param addr Address of the opcode.
 *
 * @return The length of the opcode.
 */
uint8_t cpu_get_op_len_at(uint16_t addr);

/**
 * Checks whether the current opcode is a jsr (useful for debugger to step over
 * subroutine calls).
 *
 * @return True if the opcode is a jsr.
 */
bool cpu_is_subroutine(void);

/**
 * Allows a platform to register a tick callback. This can be useful for the platform to
 * glue in timing to peripherals regardless of what process is driving the CPU.
 */
void cpu_set_tick_callback(cpu_tick_cb_t callback);
