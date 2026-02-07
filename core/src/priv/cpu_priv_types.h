#ifndef __CPU_PRIV_TYPES_H__
#define __CPU_PRIV_TYPES_H__

#include <stdint.h>
#include <stdbool.h>

typedef struct
{
    uint16_t pc;
    uint8_t sp;
    uint8_t a;
    uint8_t x;
    uint8_t y;
    uint8_t status;
} cpu_regs_t;

typedef enum
{
    BRK_VEC,
    NMI_VEC,
    RST_VEC,
    IRQ_VEC
} cpu_vec_src_t;

typedef enum {
    /* Read opcode state. */
    OPCODE,

    /* Read address mode parameters states for
     * individual clock cycles. */
    PARAM0,
    PARAM1,
    PARAM2,
    PARAM3,
    PARAM4,

    /* Opcode handler states for individual clock cycles. */
    OP0,
    OP1,
    OP2,
    OP3,
    OP4,

    VEC0,
    VEC1,
    VEC2,
    VEC3,
    VEC4,
    VEC5,
    VEC6,
} op_state_t;

typedef struct cpu_s
{
    bool init;
    cpu_regs_t regs;
    uint16_t ea;
    uint16_t reladdr;
    uint16_t value;
    uint16_t result;
    uint8_t opcode;
    uint16_t tmpval;
    bool page_boundary;
    bool cycle_consumed;
    cpu_vec_src_t vec_src;
    op_state_t op_state;
} cpu_t;

#endif /* end of include guard: __CPU_PRIV_TYPES_H__ */
