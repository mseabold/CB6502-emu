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
bool isBreakpoint(uint16_t pc);
