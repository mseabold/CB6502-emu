#ifndef __DISASSEMBLE_H__
#define __DISASSEMBLE_H__

#include "emulator.h"

/** Defines the maximum size of a disassembly string. */
#define MAX_DISASSEMBLY_STRING_LEN 32

/**
 * Used for disassembling 6502 opcode data into strictly disassembly strings.
 */
typedef struct
{
    char opcode_str[MAX_DISASSEMBLY_STRING_LEN];
} disassemble_string_t;

/**
 * Used for disassembling 6502 opcode data into meta information about each opcode
 * as well as disassembly strings.
 */
typedef struct
{
    uint8_t opcode;
    uint16_t params;
    char opcode_str[MAX_DISASSEMBLY_STRING_LEN];
} disassemble_info_t;

unsigned int disassemble_buffer_strings(uint16_t code_len, const uint8_t *code_buf, uint16_t *offset, unsigned int *num_opcodes, disassemble_string_t *strings);
unsigned int disassemble_buffer_info(uint16_t code_len, const uint8_t *code_buf, uint16_t *offset, unsigned int *num_opcodes, disassemble_info_t *info);
void disassemble_pc_string(cbemu_t emu, disassemble_string_t *str);


#endif
