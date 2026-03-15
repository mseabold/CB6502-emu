#include <string.h>
#include <stdio.h>

#include "disassemble.h"
#include "cpu_opcodes.h"
#include "bus_priv.h"
#include "cpu_priv.h"

typedef union
{
    disassemble_string_t *string;
    disassemble_info_t *info;
} disassemble_data_u_t;

const char *mnemonics[256] =
{
/*         | 0    |  1    |  2    |  3    |  4    |  5    |  6    |  7    |  8    |  9    |  A    |  B    |  C    |  D    |  E    |  F  |     */
/* 0 */      "BRK",  "ORA",  "NOP",  "NOP",  "TSB",  "ORA",  "ASL", "RMB0",  "PHP",  "ORA",  "ASL",  "NOP",  "TSB",  "ORA",  "ASL",  "BBR0", /* 0 */
/* 1 */      "BPL",  "ORA",  "ORA",  "NOP",  "TRB",  "ORA",  "ASL", "RMB1",  "CLC",  "ORA",  "NOP",  "NOP",  "TRB",  "ORA",  "ASL",  "BBR1", /* 1 */
/* 2 */      "JSR",  "AND",  "NOP",  "NOP",  "BIT",  "AND",  "ROL", "RMB2",  "PLP",  "AND",  "ROL",  "NOP",  "BIT",  "AND",  "ROL",  "BBR2", /* 2 */
/* 3 */      "BMI",  "AND",  "AND",  "NOP",  "BIT",  "AND",  "ROL", "RMB3",  "SEC",  "AND",  "NOP",  "NOP",  "BIT",  "AND",  "ROL",  "BBR3", /* 3 */
/* 4 */      "RTI",  "EOR",  "NOP",  "NOP",  "NOP",  "EOR",  "LSR", "RMB4",  "PHA",  "EOR",  "LSR",  "NOP",  "JMP",  "EOR",  "LSR",  "BBR4", /* 4 */
/* 5 */      "BVC",  "EOR",  "EOR",  "NOP",  "NOP",  "EOR",  "LSR", "RMB5",  "CLI",  "EOR",  "PHY",  "NOP",  "NOP",  "EOR",  "LSR",  "BBR5", /* 5 */
/* 6 */      "RTS",  "ADC",  "NOP",  "NOP",  "STZ",  "ADC",  "ROR", "RMB6",  "PLA",  "ADC",  "ROR",  "NOP",  "JMP",  "ADC",  "ROR",  "BBR6", /* 6 */
/* 7 */      "BVS",  "ADC",  "ADC",  "NOP",  "STZ",  "ADC",  "ROR", "RMB7",  "SEI",  "ADC",  "PLY",  "NOP",  "JMP",  "ADC",  "ROR",  "BBR7", /* 7 */
/* 8 */      "BRA",  "STA",  "NOP",  "NOP",  "STY",  "STA",  "STX", "SMB0",  "DEY",  "BIT",  "TXA",  "NOP",  "STY",  "STA",  "STX",  "BBS0", /* 8 */
/* 9 */      "BCC",  "STA",  "STA",  "NOP",  "STY",  "STA",  "STX", "SMB1",  "TYA",  "STA",  "TXS",  "NOP",  "STZ",  "STA",  "STZ",  "BBS1", /* 9 */
/* A */      "LDY",  "LDA",  "LDX",  "NOP",  "LDY",  "LDA",  "LDX", "SMB2",  "TAY",  "LDA",  "TAX",  "NOP",  "LDY",  "LDA",  "LDX",  "BBS2", /* A */
/* B */      "BCS",  "LDA",  "LDA",  "NOP",  "LDY",  "LDA",  "LDX", "SMB3",  "CLV",  "LDA",  "TSX",  "NOP",  "LDY",  "LDA",  "LDX",  "BBS3", /* B */
/* C */      "CPY",  "CMP",  "NOP",  "NOP",  "CPY",  "CMP",  "DEC", "SMB4",  "INY",  "CMP",  "DEX",  "WAI",  "CPY",  "CMP",  "DEC",  "BBS4", /* C */
/* D */      "BNE",  "CMP",  "CMP",  "NOP",  "NOP",  "CMP",  "DEC", "SMB5",  "CLD",  "CMP",  "PHX",  "STP",  "NOP",  "CMP",  "DEC",  "BBS5", /* D */
/* E */      "CPX",  "SBC",  "NOP",  "NOP",  "CPX",  "SBC",  "INC", "SMB6",  "INX",  "SBC",  "NOP",  "NOP",  "CPX",  "SBC",  "INC",  "BBS6", /* E */
/* F */      "BEQ",  "SBC",  "SBC",  "NOP",  "NOP",  "SBC",  "INC", "SMB7",  "SED",  "SBC",  "PLX",  "NOP",  "NOP",  "SBC",  "INC",  "BBS7"  /* F */
};

static uint16_t disassemble_opcode(uint16_t code_len, const uint8_t *code_buf, uint16_t code_offset, bool verbose, bool addr_valid, uint16_t addr, disassemble_data_u_t data)
{
    uint8_t opcode;
    uint8_t length;
    cpu_addr_mode_t addr_mode;
    char *dis_str;
    size_t str_len;
    uint16_t param = 0;
    int written;

    if(code_offset >= code_len)
    {
        return 0;
    }

    opcode = code_buf[code_offset];
    addr_mode = addrtable[opcode];
    length = addr_lengths[addr_mode];

    if(code_len - code_offset < length)
    {
        return 0;
    }

    dis_str = verbose ? data.info->opcode_str : data.string->opcode_str;
    str_len = MAX_DISASSEMBLY_STRING_LEN;

    /* Note, the assumption here is that no opcodes are larger then 3 bytes, which is implicitly true,
     * but not exactly enforced in code. */
    if(length > 1)
    {
        param = code_buf[code_offset+1];
    }

    if(length > 2)
    {
        param |= ((uint16_t)code_buf[code_offset+2] << 8);
    }

    if(addr_valid)
    {
        written  = snprintf(dis_str, str_len, "0x%04x: ", addr);

        if(written < 0)
        {
            return 0;
        }

        dis_str += written;
        str_len -= written;
    }

    written = snprintf(dis_str, str_len, "%s", mnemonics[opcode]);

    if(written < 0)
    {
        return 0;
    }

    dis_str += written;
    str_len -= written;

    switch(addr_mode)
    {
        case IMM:
            snprintf(dis_str, str_len, " #$%02x", (uint8_t)param);
            break;
        case ZP:
            snprintf(dis_str, str_len, " $00%02x", (uint8_t)param);
            break;
        case ZPX:
            snprintf(dis_str, str_len, " $00%02x,X", (uint8_t)param);
            break;
        case ZPY:
            snprintf(dis_str, str_len, " $00%02x,Y", (uint8_t)param);
            break;
        case REL:
            snprintf(dis_str, str_len, " $%02x", (uint8_t)param);
            break;
        case ABSO:
            snprintf(dis_str, str_len, " $%04x", param);
            break;
        case ABSX:
            snprintf(dis_str, str_len, " $%04x,X", param);
            break;
        case ABSY:
            snprintf(dis_str, str_len, " $%04x,Y", param);
            break;
        case IND:
            snprintf(dis_str, str_len, " ($%04x)", param);
            break;
        case INDX:
            snprintf(dis_str, str_len, " ($00%02x,X)", (uint8_t)param);
            break;
        case INDY:
            snprintf(dis_str, str_len, " ($00%02x,Y)", (uint8_t)param);
            break;
        case INDZ:
            snprintf(dis_str, str_len, " ($00%02x)", (uint8_t)param);
            break;
        default:
            break;
    }

    if(verbose)
    {
        data.info->opcode = opcode;
        data.info->params = param;
    }

    return length;
}

unsigned int disassemble_buffer_strings(uint16_t code_len, const uint8_t *code_buf, uint16_t *offset, unsigned int *num_opcodes, disassemble_string_t *strings)
{
    unsigned int opindex;
    uint16_t oplength;

    if((code_len == 0) || (code_buf == NULL) || (offset == NULL) || (num_opcodes == NULL) || (*num_opcodes == 0) || (strings == NULL))
    {
        return 0;
    }

    for(opindex = 0; opindex < *num_opcodes; opindex++)
    {
        oplength = disassemble_opcode(code_len, code_buf, *offset, false, false, 0, (disassemble_data_u_t)&strings[opindex]);

        if(opindex == 0)
        {
            break;
        }

        *offset += oplength;
    }

    *num_opcodes = opindex;

    return opindex;
}

unsigned int disassemble_buffer_info(uint16_t code_len, const uint8_t *code_buf, uint16_t *offset, unsigned int *num_opcodes, disassemble_info_t *info)
{
    unsigned int opindex;
    uint16_t oplength;

    if((code_len == 0) || (code_buf == NULL) || (offset == NULL) || (num_opcodes == NULL) || (*num_opcodes == 0) || (info == NULL))
    {
        return 0;
    }

    for(opindex = 0; opindex < *num_opcodes; opindex++)
    {
        oplength = disassemble_opcode(code_len, code_buf, *offset, true, false, 0, (disassemble_data_u_t)&info[opindex]);

        if(opindex == 0)
        {
            break;
        }

        *offset += oplength;
    }

    *num_opcodes = opindex;

    return opindex;
}

void disassemble_pc_string(cbemu_t emu, disassemble_string_t *str)
{
    uint16_t pc;
    uint16_t len;
    uint8_t buf[3];

    pc = CPU_GET_REG(emu, pc);

    buf[0] = bus_peek(emu, pc);
    len = 1;

    if(pc < 0xffff)
    {
        buf[1] = bus_peek(emu, pc+1);
        len++;
    }

    if(pc < 0xfffe)
    {
        buf[2] = bus_peek(emu, pc+2);
        len++;
    }

    disassemble_opcode(len, buf, 0, false, true, pc, (disassemble_data_u_t)str);

}
