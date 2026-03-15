#ifndef __CPU_OPCODES_H__
#define __CPU_OPCODES_H__

#include <stdint.h>

typedef enum {
    IMP,
    ACC,
    IMM,
    ZP,
    ZPX,
    ZPY,
    REL,
    ABSO,
    ABSX,
    ABSY,
    IND,
    INDX,
    INDY,
    INDZ,
    ABIN,
    ZPREL,
    NUM_ADDR_MODES
} cpu_addr_mode_t;

extern const cpu_addr_mode_t addrtable[256];
extern const uint8_t addr_lengths[NUM_ADDR_MODES];

#endif
