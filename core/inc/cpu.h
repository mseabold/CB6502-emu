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
/*
 * Callback function prototypes for 6502 memory space access.
 */

/**
 * Initialize the 6502 emulator. This sets the memory access functions as well as performs
 * an optional initial reset6502().
 *
 * @param mem_space Handler table for the systems memory bus interface.
 * @param reset     Perform a reset6502 as well as initialization.
 */
void init6502(mem_space_t *mem_space, bool reset);

/**
 * Resets the 6502 emulator and forces it to process the reset vector.
 */
void reset6502(void);

/**
 * Executes 6502 code up to the next specified count of clock cycles.
 */
void exec6502(uint32_t tickcount);

/**
 * Executes a single instruction
 */
void step6502(void);


/** TODO interrupts
 * I want to eventually change this to a voting mechanism so that different modules
 * can "pull down" the IRQ line and the IRQ handler will continue to be processed
 * since it is level triggered.
 */
void disassemble(size_t bufLen, char *buf);

uint16_t cpu_get_reg(cpu_reg_t reg);
void cpu_get_regs(cpu_regs_t *regs);

uint8_t cpu_get_op_len(void);
bool cpu_is_subroutine(void);
