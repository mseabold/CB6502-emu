/*
 * (c) 2022 Matt Seabold
 *
 * Originally based on Make Chamber's Fake6502:
 *
 * (c)2011 Mike Chambers (miker00lz@gmail.com)
 * http://rubbermallet.org/fake6502.c
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "cpu_priv.h"
#include "bus_priv.h"
#include "clock_priv.h"

#define FLAG_CARRY     0x01
#define FLAG_ZERO      0x02
#define FLAG_INTERRUPT 0x04
#define FLAG_DECIMAL   0x08
#define FLAG_BREAK     0x10
#define FLAG_CONSTANT  0x20
#define FLAG_OVERFLOW  0x40
#define FLAG_SIGN      0x80

#define BASE_STACK     0x100

#define saveaccum(cpu) cpu.regs.a = (uint8_t)((cpu.result) & 0x00FF)


//flag modifier macros
#define setcarry(status) status |= FLAG_CARRY
#define clearcarry(status) status &= (~FLAG_CARRY)
#define setzero(status) status |= FLAG_ZERO
#define clearzero(status) status &= (~FLAG_ZERO)
#define setinterrupt(status) status |= FLAG_INTERRUPT
#define clearinterrupt(status) status &= (~FLAG_INTERRUPT)
#define setdecimal(status) status |= FLAG_DECIMAL
#define cleardecimal(status) status &= (~FLAG_DECIMAL)
#define setoverflow(status) status |= FLAG_OVERFLOW
#define clearoverflow(status) status &= (~FLAG_OVERFLOW)
#define setsign(status) status |= FLAG_SIGN
#define clearsign(status) status &= (~FLAG_SIGN)


//flag calculation macros
#define zerocalc(status, n) \
    do { \
        if((n) & 0x00FF) \
            clearzero(status); \
        else \
            setzero(status); \
    } while(0)

#define signcalc(status, n) \
    do { \
        if((n) & 0x0080) \
            setsign(status); \
        else \
            clearsign(status); \
    } while(0)

#define carrycalc(status, n) \
    do { \
    if((n) & 0xFF00) \
        setcarry(status); \
    else \
        clearcarry(status); \
    } while(0)

/* n = result, m = accumulator, o = memory */
#define overflowcalc(status, n, m, o) \
    do { \
        if(((n) ^ (uint16_t)(m)) & ((n) ^ (o)) & 0x0080) \
            setoverflow(status); \
        else \
            clearoverflow(status); \
    } while(0)

//a few general functions used by various other functions
void push8(cbemu_t emu, uint8_t pushval)
{
    bus_write(emu, BASE_STACK + emu->cpu.regs.sp--, pushval);
}

uint8_t pull8(cbemu_t emu)
{
    return bus_read(emu, BASE_STACK + ++emu->cpu.regs.sp);
}

void cpu_reset()
{
#if 0
    pc = (uint16_t)bus_read(emu, 0xFFFC) | ((uint16_t)bus_read(emu, 0xFFFD) << 8);
    a = 0;
    x = 0;
    y = 0;
    sp = 0xFD;
    status |= FLAG_CONSTANT;
#endif
}


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

const cpu_addr_mode_t addrtable[256];
static void (*optable[256])(cbemu_t);
static uint8_t penaltyop, penaltyaddr;
static const uint8_t branch_shift_map[] = {
    7, /* N */
    6, /* V */
    0, /* C */
    1  /* Z */
};

static inline void advance_state(cpu_t *cpu, op_state_t new_state, bool memcycle)
{
    cpu->cycle_consumed = memcycle;
    cpu->op_state = new_state;
}

//addressing mode functions, calculates effective addresses

//implied
static void imp(cbemu_t emu)
{
    /* Implied opcodes take 2 cycles, reading the next PC an additional time. */
    (void)bus_read(emu, emu->cpu.regs.pc);
    advance_state(&emu->cpu, OP0, false);
}

//accumulator
static void acc(cbemu_t emu)
{
    /* Implied opcodes take 2 cycles, reading the next PC an additional time. */
    (void)bus_read(emu, emu->cpu.regs.pc);
    advance_state(&emu->cpu, OP0, false);
}

//immediate
static void imm(cbemu_t emu)
{
    emu->cpu.ea = emu->cpu.regs.pc++;

    /* The read of the effective address will consume the second cycle. */
    advance_state(&emu->cpu, OP0, false);
}

//zero-page
static void zp(cbemu_t emu)
{
    emu->cpu.ea = (uint16_t)bus_read(emu, (uint16_t)emu->cpu.regs.pc++);
    advance_state(&emu->cpu, OP0, true);
}

//zero-page,X
static void zpx(cbemu_t emu)
{
    if(emu->cpu.op_state == PARAM0)
    {
        emu->cpu.ea = bus_read(emu, emu->cpu.regs.pc);
        advance_state(&emu->cpu, PARAM1, true);
    }
    else
    {
        emu->cpu.ea = (emu->cpu.ea + (uint16_t)emu->cpu.regs.x) & 0x00FF;
        (void)bus_read(emu, emu->cpu.regs.pc++);
        advance_state(&emu->cpu, OP0, true);
    }
}

//zero-page,Y
static void zpy(cbemu_t emu)
{
    if(emu->cpu.op_state == PARAM0)
    {
        emu->cpu.ea = bus_read(emu, emu->cpu.regs.pc);
        advance_state(&emu->cpu, PARAM1, true);
    }
    else
    {
        emu->cpu.ea = (emu->cpu.ea + (uint16_t)emu->cpu.regs.y) & 0x00FF;
        (void)bus_read(emu, emu->cpu.regs.pc++);
        advance_state(&emu->cpu, OP0, true);
    }
}

//relative for branch ops (8-bit immediate value, sign-extended)
static void rel(cbemu_t emu)
{
    emu->cpu.reladdr = (uint16_t)bus_read(emu, emu->cpu.regs.pc++);
    if(emu->cpu.reladdr & 0x80)
        emu->cpu.reladdr |= 0xFF00;

    advance_state(&emu->cpu, OP0, false);
}

//absolute
static void abso(cbemu_t emu)
{
    if(emu->cpu.opcode == 0x20)
    {
        /* JSR is abso-like in that it has a 2 byte absolute address
         * parameter. But it acts differently. Let it handle itself.
         */
        advance_state(&emu->cpu, OP0, false);
        return;
    }

    if(emu->cpu.op_state == PARAM0)
    {
        emu->cpu.ea = (uint16_t)bus_read(emu, emu->cpu.regs.pc++);
        advance_state(&emu->cpu, PARAM1, true);
    }
    else
    {
        emu->cpu.ea |= ((uint16_t)bus_read(emu, emu->cpu.regs.pc++)) << 8;

        /* JMP a is special in that there is no cycle after the immediate
         * address read. For it specifically, do not consume a cycle here.
         */
        if(emu->cpu.opcode == 0x4c)
        {
            advance_state(&emu->cpu, OP0, false);
        }
        else
        {
            advance_state(&emu->cpu, OP0, true);
        }
    }
}

//absolute,X
static void absx(cbemu_t emu)
{
    uint16_t startpage;

    if(emu->cpu.op_state == PARAM0)
    {
        emu->cpu.ea = (uint16_t)bus_read(emu, emu->cpu.regs.pc++);
        advance_state(&emu->cpu, PARAM1, true);
    }
    else if(emu->cpu.op_state == PARAM1)
    {
        emu->cpu.ea |= (uint16_t)bus_read(emu, emu->cpu.regs.pc) << 8;

        startpage = emu->cpu.ea & 0xFF00;
        emu->cpu.ea += (uint16_t)emu->cpu.regs.x;

        if(startpage != (emu->cpu.ea & 0xFF00))
        {
            /* Absolute address crossed pages. We need to eat one more penalty
             * cycle. */
            emu->cpu.page_boundary = true;
            advance_state(&emu->cpu, PARAM2, true);
        }
        else
        {
            ++emu->cpu.regs.pc;
            advance_state(&emu->cpu, OP0, true);
        }
    }
    else
    {
        /* Penalty cycle. Repeat read cycle on current PC. */
        (void)bus_read(emu, emu->cpu.regs.pc++);
        advance_state(&emu->cpu, OP0, true);
    }
}

//absolute,Y
static void absy(cbemu_t emu)
{
    uint16_t startpage;

    if(emu->cpu.op_state == PARAM0)
    {
        emu->cpu.ea = (uint16_t)bus_read(emu, emu->cpu.regs.pc++);
        advance_state(&emu->cpu, PARAM1, true);
    }
    else if(emu->cpu.op_state == PARAM1)
    {
        emu->cpu.ea |= (uint16_t)bus_read(emu, emu->cpu.regs.pc) << 8;

        startpage = emu->cpu.ea & 0xFF00;
        emu->cpu.ea += (uint16_t)emu->cpu.regs.y;

        if(startpage != (emu->cpu.ea & 0xFF00))
        {
            /* Absolute address crossed pages. We need to eat one more penalty
             * cycle. */
            emu->cpu.page_boundary = true;
            advance_state(&emu->cpu, PARAM2, true);
        }
        else
        {
            ++emu->cpu.regs.pc;
            advance_state(&emu->cpu, OP0, true);
        }
    }
    else
    {
        /* Penalty cycle. Repeat read cycle on current PC. */
        (void)bus_read(emu, emu->cpu.regs.pc++);
        advance_state(&emu->cpu, OP0, true);
    }
}

//indirect
static void ind(cbemu_t emu)
{
    switch(emu->cpu.op_state)
    {
        case PARAM0:
            emu->cpu.tmpval = (uint16_t)bus_read(emu, emu->cpu.regs.pc++);
            advance_state(&emu->cpu, PARAM1, true);
            break;
        case PARAM1:
            emu->cpu.tmpval |= (uint16_t)bus_read(emu, emu->cpu.regs.pc) << 8;
            advance_state(&emu->cpu, PARAM2, true);
            break;
        case PARAM2:
            /* 65C02 always takes an extra cycle here. This is the workaround to the
             * NMOS 6502 issue if page-wrap on this on this opcode. */
            (void)bus_read(emu, emu->cpu.regs.pc);
            advance_state(&emu->cpu, PARAM3, true);
            break;
        case PARAM3:
            emu->cpu.ea = (uint16_t)bus_read(emu, emu->cpu.tmpval);
            advance_state(&emu->cpu, PARAM4, true);
            break;
        case PARAM4:
            emu->cpu.ea |= (uint16_t)bus_read(emu, emu->cpu.tmpval+1) << 8;
            advance_state(&emu->cpu, OP0, false);
            break;
        default:
            break;
    }
}

// (indirect,X)
static void indx(cbemu_t emu)
{
    switch(emu->cpu.op_state)
    {
        case PARAM0:
            emu->cpu.tmpval = bus_read(emu, emu->cpu.regs.pc);
            advance_state(&emu->cpu, PARAM1, true);
            break;
        case PARAM1:
            emu->cpu.tmpval = (emu->cpu.tmpval + (uint16_t)emu->cpu.regs.x) & 0x00FF;
            (void)bus_read(emu, emu->cpu.regs.pc++);
            advance_state(&emu->cpu, PARAM2, true);
            break;
        case PARAM2:
            emu->cpu.ea = bus_read(emu, emu->cpu.tmpval);
            emu->cpu.tmpval = (emu->cpu.tmpval + 1) & 0x00FF;
            advance_state(&emu->cpu, PARAM3, true);
            break;
        case PARAM3:
            emu->cpu.ea |= (uint16_t)bus_read(emu, emu->cpu.tmpval) << 8;
            advance_state(&emu->cpu, OP0, true);
            break;
        default:
            break;
    }
}

// (indirect),Y
static void indy(cbemu_t emu)
{
    uint16_t startpage;
    switch(emu->cpu.op_state)
    {
        case PARAM0:
            emu->cpu.tmpval = bus_read(emu, emu->cpu.regs.pc++);
            advance_state(&emu->cpu, PARAM1, true);
            break;
        case PARAM1:
            emu->cpu.ea = bus_read(emu, emu->cpu.tmpval);
            emu->cpu.tmpval = (emu->cpu.tmpval + 1) & 0x00FF;
            advance_state(&emu->cpu, PARAM2, true);
            break;
        case PARAM2:
            emu->cpu.ea |= bus_read(emu, emu->cpu.tmpval) << 8;
            startpage = emu->cpu.ea & 0xFF00;

            emu->cpu.ea += (uint16_t)emu->cpu.regs.y;

            if(startpage != (emu->cpu.ea & 0xFF00))
            {
                emu->cpu.page_boundary = true;
                advance_state(&emu->cpu, PARAM3, true);
            }
            else
            {
                advance_state(&emu->cpu, OP0, true);
            }
            break;
        case PARAM3:
            /* Repeat the second zeropage read on page cross. */
            (void)bus_read(emu, emu->cpu.tmpval);
            advance_state(&emu->cpu, OP0, true);
            break;
        default:
            break;
    }
}

// (zp)
static void indz(cbemu_t emu)
{
    switch(emu->cpu.op_state)
    {
        case PARAM0:
            emu->cpu.tmpval = bus_read(emu, emu->cpu.regs.pc++);
            advance_state(&emu->cpu, PARAM1, true);
            break;
        case PARAM1:
            emu->cpu.ea = bus_read(emu, emu->cpu.tmpval);
            emu->cpu.tmpval = (emu->cpu.tmpval + 1) & 0xFF;
            advance_state(&emu->cpu, PARAM2, true);
            break;
        case PARAM2:
            emu->cpu.ea |= ((uint16_t)bus_read(emu, emu->cpu.tmpval)) << 8;
            advance_state(&emu->cpu, OP0, true);
            break;
        default:
            break;
    }
}

// (a,x)
static void abin(cbemu_t emu)
{
    switch(emu->cpu.op_state)
    {
        case PARAM0:
            emu->cpu.tmpval = bus_read(emu, emu->cpu.regs.pc++);
            advance_state(&emu->cpu, PARAM1, true);
            break;
        case PARAM1:
            /* Note: PC does not seem to increment after second imm read.
             *       The only opcode that uses this mode is jmp (a,x), so
             *       the PC is overwritten anyways. */
            emu->cpu.tmpval |= (uint16_t)bus_read(emu, emu->cpu.regs.pc) << 8;
            advance_state(&emu->cpu, PARAM2, true);
            break;
        case PARAM2:
            /* Bus re-reads same byte. Presumably this is to add X, handle
             * carry for indexing. */
            (void)bus_read(emu, emu->cpu.regs.pc);
            emu->cpu.tmpval += (uint16_t)emu->cpu.regs.x;
            advance_state(&emu->cpu, PARAM3, true);
            break;
        case PARAM3:
            emu->cpu.ea = bus_read(emu, emu->cpu.tmpval);
            advance_state(&emu->cpu, PARAM4, true);
            break;
        case PARAM4:
            emu->cpu.ea |= (uint16_t)bus_read(emu, emu->cpu.tmpval+1) << 8;
            advance_state(&emu->cpu, OP0, false);
            break;
        default:
            break;
    }
}

static void zprel(cbemu_t emu)
{
    switch(emu->cpu.op_state)
    {
        case PARAM0:
            emu->cpu.ea = bus_read(emu, emu->cpu.regs.pc++);
            advance_state(&emu->cpu, PARAM1, true);
            break;
        case PARAM1:
            /* Read the value from zp before reading the relative branch. */
            emu->cpu.value = bus_read(emu, emu->cpu.ea);
            advance_state(&emu->cpu, PARAM2, true);
            break;
        case PARAM2:
            /* TODO This needs verification. The opcode reads from the ea twice. The question is
             *      whether the second read is ignored or can actually affect the result if the
             *      value changes. For now, assume it is ignored.
             */
            (void)bus_read(emu, emu->cpu.ea);
            advance_state(&emu->cpu, PARAM3, true);
            break;
        case PARAM3:
            emu->cpu.reladdr = bus_read(emu, emu->cpu.regs.pc++);
            advance_state(&emu->cpu, OP0, false);
            break;
        default:
            break;
    }
}

static uint16_t getvalue(cbemu_t emu)
{
    uint16_t value;

    if(addrtable[emu->cpu.opcode] == ACC)
    {
        value = (uint16_t)emu->cpu.regs.a;
    }
    else
    {
        value = (uint16_t)bus_read(emu, emu->cpu.ea);
    }

    return value;
}

static void putvalue(cbemu_t emu, uint16_t saveval)
{
    if(addrtable[emu->cpu.opcode] == ACC)
    {
        emu->cpu.regs.a = (uint8_t)(saveval & 0x00FF);
    }
    else
    {
        bus_write(emu, emu->cpu.ea, (saveval & 0x00FF));
    }
}


//instruction handler functions
static void adc(cbemu_t emu)
{
    uint16_t value;

    if(emu->cpu.op_state == OP0)
    {
        value = getvalue(emu);

        emu->cpu.result = (uint16_t)emu->cpu.regs.a + value + (uint16_t)(emu->cpu.regs.status & FLAG_CARRY);

        carrycalc(emu->cpu.regs.status, emu->cpu.result);
        zerocalc(emu->cpu.regs.status, emu->cpu.result);
        overflowcalc(emu->cpu.regs.status, emu->cpu.result, emu->cpu.regs.a, value);
        signcalc(emu->cpu.regs.status, emu->cpu.result);

        if(emu->cpu.regs.status & FLAG_DECIMAL)
        {
            advance_state(&emu->cpu, OP1, true);
        }
        else
        {
            saveaccum(emu->cpu);
            advance_state(&emu->cpu, OPCODE, true);
        }
    }
    else
    {
        /* Only takes a second cycle for decimal mode. */
        clearcarry(emu->cpu.regs.status);

        if((emu->cpu.result & 0x0F) > 0x09)
        {
            emu->cpu.result += 0x06;
        }
        if((emu->cpu.result & 0xF0) > 0x90)
        {
            emu->cpu.result += 0x60;
            setcarry(emu->cpu.regs.status);
        }

        saveaccum(emu->cpu);
        advance_state(&emu->cpu, OPCODE, true);
    }
}

static void and(cbemu_t emu)
{
    uint16_t value, result;

    value = getvalue(emu);

    result = (uint16_t)emu->cpu.regs.a & value;

    zerocalc(emu->cpu.regs.status, result);
    signcalc(emu->cpu.regs.status, result);

    saveaccum(emu->cpu);
    advance_state(&emu->cpu, OPCODE, true);
}

static void asl(cbemu_t emu)
{
    uint16_t value;

    switch(emu->cpu.op_state)
    {
        case OP0:
            value = getvalue(emu);

            emu->cpu.result = value << 1;

            carrycalc(emu->cpu.regs.status, emu->cpu.result);
            zerocalc(emu->cpu.regs.status, emu->cpu.result);
            signcalc(emu->cpu.regs.status, emu->cpu.result);

            if(addrtable[emu->cpu.opcode] == ACC)
            {
                /* Accumulator, so no additional cycles to consume. */
                putvalue(emu, emu->cpu.result);
                advance_state(&emu->cpu, OPCODE, true);
            }
            else
            {
                advance_state(&emu->cpu, OP1, true);
            }
            break;
        case OP1:
            /* Re-read the ea. */
            (void)bus_read(emu, emu->cpu.ea);
            advance_state(&emu->cpu, OP2, true);
            break;
        case OP2:
            putvalue(emu, emu->cpu.result);
            advance_state(&emu->cpu, OPCODE, true);
            break;
        default:
            break;
    }
}

static void bxx(cbemu_t emu)
{
    uint8_t exp_flag;
    uint8_t flag_shift;

    switch(emu->cpu.op_state)
    {
        case OP0:
            /* The top 2 bits of the opcode indicate which flag is being checked, and bit 6
             * indicates whether the bit should be set. */
            flag_shift = branch_shift_map[(emu->cpu.opcode & 0xc0) >> 6];
            exp_flag = (emu->cpu.opcode & 0x20) >> 5;

            if(((emu->cpu.regs.status >> flag_shift) & 0x01) == exp_flag)
            {
                /* PC stays the same from the bus standpoint. */
                advance_state(&emu->cpu, OP1, true);
            }
            else
            {
                advance_state(&emu->cpu, OPCODE, true);
            }
            break;
        case OP1:
            (void)bus_read(emu, emu->cpu.regs.pc);

            if((emu->cpu.regs.pc & 0xFF00) != ((emu->cpu.regs.pc + emu->cpu.reladdr) & 0xFF00))
            {
                /* Branch jumps a page, so we need to eat another cycle (PC
                 * again stays the same. */
                advance_state(&emu->cpu, OP2, true);
            }
            else
            {
                emu->cpu.regs.pc += emu->cpu.reladdr;
                advance_state(&emu->cpu, OPCODE, true);
            }
            break;
        case OP2:
            (void)bus_read(emu, emu->cpu.regs.pc);
            emu->cpu.regs.pc += emu->cpu.reladdr;
            advance_state(&emu->cpu, OPCODE, true);
            break;
        default:
            break;
    }
}

static void bit(cbemu_t emu)
{
    uint16_t value, result;

    value = getvalue(emu);
    result = (uint16_t)emu->cpu.regs.a & value;

    zerocalc(emu->cpu.regs.status, result);
    emu->cpu.regs.status = (emu->cpu.regs.status & 0x3F) | (uint8_t)(value & 0xC0);

    advance_state(&emu->cpu, OPCODE, true);
}

static void bra(cbemu_t emu)
{
    switch(emu->cpu.op_state)
    {
        case OP0:
            /* Consume the relative addr read cycle. */
            advance_state(&emu->cpu, OP1, true);
            break;
        case OP1:
            (void)bus_read(emu, emu->cpu.regs.pc);

            if((emu->cpu.regs.pc & 0xFF00) != ((emu->cpu.regs.pc + emu->cpu.reladdr) & 0xFF00))
            {
                advance_state(&emu->cpu, OP2, true);
            }
            else
            {
                emu->cpu.regs.pc += emu->cpu.reladdr;
                advance_state(&emu->cpu, OPCODE, true);
            }
            break;
        case OP2:
            (void)bus_read(emu, emu->cpu.regs.pc);
            emu->cpu.regs.pc += emu->cpu.reladdr;
            advance_state(&emu->cpu, OPCODE, true);
            break;
        default:
            break;
    }
}

static void brk(cbemu_t emu)
{
    /* Consume the second PC read, then let the vector
     * state machine take over. */
    advance_state(&emu->cpu, VEC2, true);
    emu->cpu.vec_src = BRK_VEC;
}

static void clc(cbemu_t emu)
{
    clearcarry(emu->cpu.regs.status);
    advance_state(&emu->cpu, OPCODE, true);
}

static void cld(cbemu_t emu)
{
    cleardecimal(emu->cpu.regs.status);
    advance_state(&emu->cpu, OPCODE, true);
}

static void cli(cbemu_t emu)
{
    clearinterrupt(emu->cpu.regs.status);
    advance_state(&emu->cpu, OPCODE, true);
}

static void clv(cbemu_t emu)
{
    clearoverflow(emu->cpu.regs.status);
    advance_state(&emu->cpu, OPCODE, true);
}

static void cmp(cbemu_t emu)
{
    uint16_t value, result;

    value = getvalue(emu);
    result = (uint16_t)emu->cpu.regs.a - value;

    if(emu->cpu.regs.a >= (uint8_t)(value & 0x00FF))
        setcarry(emu->cpu.regs.status);
    else
        clearcarry(emu->cpu.regs.status);

    if(emu->cpu.regs.a == (uint8_t)(value & 0x00FF))
        setzero(emu->cpu.regs.status);
    else
        clearzero(emu->cpu.regs.status);

    signcalc(emu->cpu.regs.status, result);
    advance_state(&emu->cpu, OPCODE, true);
}

static void cpx(cbemu_t emu)
{
    uint16_t value, result;

    value = getvalue(emu);
    result = (uint16_t)emu->cpu.regs.x - value;

    if(emu->cpu.regs.x >= (uint8_t)(value & 0x00FF))
        setcarry(emu->cpu.regs.status);
    else
        clearcarry(emu->cpu.regs.status);

    if(emu->cpu.regs.x == (uint8_t)(value & 0x00FF))
        setzero(emu->cpu.regs.status);
    else
        clearzero(emu->cpu.regs.status);

    signcalc(emu->cpu.regs.status, result);
    advance_state(&emu->cpu, OPCODE, true);
}

static void cpy(cbemu_t emu)
{
    uint16_t value, result;

    value = getvalue(emu);
    result = (uint16_t)emu->cpu.regs.y - value;

    if(emu->cpu.regs.y >= (uint8_t)(value & 0x00FF))
        setcarry(emu->cpu.regs.status);
    else
        clearcarry(emu->cpu.regs.status);

    if(emu->cpu.regs.y == (uint8_t)(value & 0x00FF))
        setzero(emu->cpu.regs.status);
    else
        clearzero(emu->cpu.regs.status);

    signcalc(emu->cpu.regs.status, result);
    advance_state(&emu->cpu, OPCODE, true);
}

static void dec(cbemu_t emu)
{
    uint16_t value;

    switch(emu->cpu.op_state)
    {
        case OP0:
            /* There is a fixed penalty cycle here for some addressing modes. For a page boundary,
             * it has already been consumed. If not, we need to consumed it here with an additonal
             * EA read. */
            if(addrtable[emu->cpu.opcode] == ABSX && !emu->cpu.page_boundary)
            {
                (void)bus_read(emu, emu->cpu.ea);
                advance_state(&emu->cpu, OP1, true);
            }
            else
            {
                /* Advance without consuming a cycle. */
                advance_state(&emu->cpu, OP1, false);
            }
            break;
        case OP1:
            value = getvalue(emu);
            emu->cpu.result = value - 1;

            zerocalc(emu->cpu.regs.status, emu->cpu.result);
            signcalc(emu->cpu.regs.status, emu->cpu.result);

            if(addrtable[emu->cpu.opcode] == ACC)
            {
                putvalue(emu, emu->cpu.result);
                advance_state(&emu->cpu, OPCODE, true);
            }
            else
            {
                advance_state(&emu->cpu, OP2, true);
            }
            break;
        case OP2:
            (void)bus_read(emu, emu->cpu.ea);
            advance_state(&emu->cpu, OP3, true);
            break;
        case OP3:
            putvalue(emu, emu->cpu.result);
            advance_state(&emu->cpu, OPCODE, true);
            break;
        default:
            break;
    }
}

static void dex(cbemu_t emu)
{
    emu->cpu.regs.x--;

    zerocalc(emu->cpu.regs.status, emu->cpu.regs.x);
    signcalc(emu->cpu.regs.status, emu->cpu.regs.x);

    advance_state(&emu->cpu, OPCODE, true);
}

static void dey(cbemu_t emu)
{
    emu->cpu.regs.y--;

    zerocalc(emu->cpu.regs.status, emu->cpu.regs.y);
    signcalc(emu->cpu.regs.status, emu->cpu.regs.y);

    advance_state(&emu->cpu, OPCODE, true);
}

static void eor(cbemu_t emu)
{
    uint16_t value, result;

    value = getvalue(emu);
    result = (uint16_t)emu->cpu.regs.a ^ value;

    zerocalc(emu->cpu.regs.status, result);
    signcalc(emu->cpu.regs.status, result);

    saveaccum(emu->cpu);
    advance_state(&emu->cpu, OPCODE, true);
}

static void inc(cbemu_t emu)
{
    uint16_t value;

    switch(emu->cpu.op_state)
    {
        case OP0:
            /* There is a fixed penalty cycle here for some addressing modes. For a page boundary,
             * it has already been consumed. If not, we need to consumed it here with an additonal
             * EA read. */
            if(addrtable[emu->cpu.opcode] == ABSX && !emu->cpu.page_boundary)
            {
                (void)bus_read(emu, emu->cpu.ea);
                advance_state(&emu->cpu, OP1, true);
            }
            else
            {
                /* Advance without consuming a cycle. */
                advance_state(&emu->cpu, OP1, false);
            }
            break;
        case OP1:
            value = getvalue(emu);
            emu->cpu.result = value + 1;

            zerocalc(emu->cpu.regs.status, emu->cpu.result);
            signcalc(emu->cpu.regs.status, emu->cpu.result);

            if(addrtable[emu->cpu.opcode] == ACC)
            {
                putvalue(emu, emu->cpu.result);
                advance_state(&emu->cpu, OPCODE, true);
            }
            else
                advance_state(&emu->cpu, OP2, true);
            break;
        case OP2:
            (void)bus_read(emu, emu->cpu.ea);
            advance_state(&emu->cpu, OP3, true);
            break;
        case OP3:
            putvalue(emu, emu->cpu.result);
            advance_state(&emu->cpu, OPCODE, true);
            break;
        default:
            break;
    }
}

static void inx(cbemu_t emu)
{
    emu->cpu.regs.x++;

    zerocalc(emu->cpu.regs.status, emu->cpu.regs.x);
    signcalc(emu->cpu.regs.status, emu->cpu.regs.x);

    advance_state(&emu->cpu, OPCODE, true);
}

static void iny(cbemu_t emu)
{
    emu->cpu.regs.y++;

    zerocalc(emu->cpu.regs.status, emu->cpu.regs.y);
    signcalc(emu->cpu.regs.status, emu->cpu.regs.y);

    advance_state(&emu->cpu, OPCODE, true);
}

static void jmp(cbemu_t emu)
{
    emu->cpu.regs.pc = emu->cpu.ea;
    advance_state(&emu->cpu, OPCODE, true);
}

static void jsr(cbemu_t emu)
{
    switch(emu->cpu.op_state)
    {
        case OP0:
            emu->cpu.ea = bus_read(emu, emu->cpu.regs.pc++);
            advance_state(&emu->cpu, OP1, true);
            break;
        case OP1:
            /* Ignored stack read. */
            (void)bus_read(emu, BASE_STACK + emu->cpu.regs.sp);
            advance_state(&emu->cpu, OP2, true);
            break;
        case OP2:
            push8(emu, (emu->cpu.regs.pc & 0xFF00) >> 8);
            advance_state(&emu->cpu, OP3, true);
            break;
        case OP3:
            push8(emu, emu->cpu.regs.pc & 0xFF);
            advance_state(&emu->cpu, OP4, true);
            break;
        case OP4:
            emu->cpu.ea |= (uint16_t)bus_read(emu, emu->cpu.regs.pc) << 8;
            emu->cpu.regs.pc = emu->cpu.ea;
            advance_state(&emu->cpu, OPCODE, true);
            break;
        default:
            break;
    }
}

static void lda(cbemu_t emu)
{
    uint16_t value;

    value = getvalue(emu);
    emu->cpu.regs.a = (uint8_t)(value & 0x00FF);

    zerocalc(emu->cpu.regs.status, emu->cpu.regs.a);
    signcalc(emu->cpu.regs.status, emu->cpu.regs.a);
    advance_state(&emu->cpu, OPCODE, true);
}

static void ldx(cbemu_t emu)
{
    uint16_t value;

    value = getvalue(emu);
    emu->cpu.regs.x = (uint8_t)(value & 0x00FF);

    zerocalc(emu->cpu.regs.status, emu->cpu.regs.x);
    signcalc(emu->cpu.regs.status, emu->cpu.regs.x);
    advance_state(&emu->cpu, OPCODE, true);
}

static void ldy(cbemu_t emu)
{
    uint16_t value;

    value = getvalue(emu);
    emu->cpu.regs.y = (uint8_t)(value & 0x00FF);

    zerocalc(emu->cpu.regs.status, emu->cpu.regs.y);
    signcalc(emu->cpu.regs.status, emu->cpu.regs.y);
    advance_state(&emu->cpu, OPCODE, true);
}

static void lsr(cbemu_t emu)
{
    uint16_t value;

    switch(emu->cpu.op_state)
    {
        case OP0:
            value = getvalue(emu);
            emu->cpu.result = value >> 1;

            if(value & 1)
                setcarry(emu->cpu.regs.status);
            else
                clearcarry(emu->cpu.regs.status);

            zerocalc(emu->cpu.regs.status, emu->cpu.result);
            signcalc(emu->cpu.regs.status, emu->cpu.result);

            if(addrtable[emu->cpu.opcode] == ACC)
            {
                putvalue(emu, emu->cpu.result);
                advance_state(&emu->cpu, OPCODE, true);
            }
            else
            {
                advance_state(&emu->cpu, OP1, true);
            }

            break;
        case OP1:
            (void)bus_read(emu, emu->cpu.ea);
            advance_state(&emu->cpu, OP2, true);
            break;
        case OP2:
            putvalue(emu, emu->cpu.result);
            advance_state(&emu->cpu, OPCODE, true);
            break;
        default:
            break;
    }
}

static void nop(cbemu_t emu)
{
    /* TODO cycle counts for undocumented nops? */
    advance_state(&emu->cpu, OPCODE, true);
}

static void ora(cbemu_t emu)
{
    uint16_t value;

    value = getvalue(emu);
    emu->cpu.result = (uint16_t)emu->cpu.regs.a | value;

    zerocalc(emu->cpu.regs.status, emu->cpu.result);
    signcalc(emu->cpu.regs.status, emu->cpu.result);

    saveaccum(emu->cpu);
    advance_state(&emu->cpu, OPCODE, true);
}

static void ph_(cbemu_t emu)
{
    if(emu->cpu.op_state == OP0)
    {
        /* Consume the immediate (next PC) read. */
        advance_state(&emu->cpu, OP1, true);
    }
    else
    {
        switch(emu->cpu.opcode)
        {
            case 0x08:
                push8(emu, emu->cpu.regs.status | FLAG_BREAK);
                break;
            case 0x48:
                push8(emu, emu->cpu.regs.a);
                break;
            case 0x5A:
                push8(emu, emu->cpu.regs.y);
                break;
            case 0xDA:
                push8(emu, emu->cpu.regs.x);
                break;
            default:
                break;
        }

        advance_state(&emu->cpu, OPCODE, true);
    }
}

static void pl_(cbemu_t emu)
{
    switch(emu->cpu.op_state)
    {
        case OP0:
            /* Consume the immediate read state. */
            advance_state(&emu->cpu, OP1, true);
            break;
        case OP1:
            /* Read the current stack pointer before it increments. */
            (void)bus_read(emu, BASE_STACK + emu->cpu.regs.sp);
            advance_state(&emu->cpu, OP2, true);
            break;
        case OP2:
            switch(emu->cpu.opcode)
            {
                case 0x28:
                    emu->cpu.regs.status = pull8(emu) | FLAG_CONSTANT; // TODO Ignore Break?
                    break;
                case 0x68:
                    emu->cpu.regs.a = pull8(emu);
                    zerocalc(emu->cpu.regs.status, emu->cpu.regs.a);
                    signcalc(emu->cpu.regs.status, emu->cpu.regs.a);
                    break;
                case 0x7A:
                    emu->cpu.regs.y = pull8(emu);
                    zerocalc(emu->cpu.regs.status, emu->cpu.regs.y);
                    signcalc(emu->cpu.regs.status, emu->cpu.regs.y);
                    break;
                case 0xFA:
                    emu->cpu.regs.x = pull8(emu);
                    zerocalc(emu->cpu.regs.status, emu->cpu.regs.x);
                    signcalc(emu->cpu.regs.status, emu->cpu.regs.x);
                    break;
                default:
                    break;
            }

            advance_state(&emu->cpu, OPCODE, true);
            break;
        default:
            break;
    }
}

static void rol(cbemu_t emu)
{
    uint16_t value;
    switch(emu->cpu.op_state)
    {
        case OP0:
            value = getvalue(emu);
            emu->cpu.result = (value << 1) | (emu->cpu.regs.status & FLAG_CARRY);

            carrycalc(emu->cpu.regs.status, emu->cpu.result);
            zerocalc(emu->cpu.regs.status, emu->cpu.result);
            signcalc(emu->cpu.regs.status, emu->cpu.result);

            if(addrtable[emu->cpu.opcode] == ACC)
            {
                putvalue(emu, emu->cpu.result);
                advance_state(&emu->cpu, OPCODE, true);
            }
            else
            {
                advance_state(&emu->cpu, OP1, true);
            }
            break;
        case OP1:
            (void)bus_read(emu, emu->cpu.ea);
            advance_state(&emu->cpu, OP2, true);
            break;
        case OP2:
            putvalue(emu, emu->cpu.result);
            advance_state(&emu->cpu, OPCODE, true);
            break;
        default:
            break;
    }
}

static void ror(cbemu_t emu)
{
    uint16_t value;
    switch(emu->cpu.op_state)
    {
        case OP0:
            value = getvalue(emu);
            emu->cpu.result = (value >> 1) | ((emu->cpu.regs.status & FLAG_CARRY) << 7);

            if(value & 1)
                setcarry(emu->cpu.regs.status);
            else
                clearcarry(emu->cpu.regs.status);

            zerocalc(emu->cpu.regs.status, emu->cpu.result);
            signcalc(emu->cpu.regs.status, emu->cpu.result);

            if(addrtable[emu->cpu.opcode] == ACC)
            {
                putvalue(emu, emu->cpu.result);
                advance_state(&emu->cpu, OPCODE, true);
            }
            else
            {
                advance_state(&emu->cpu, OP1, true);
            }
            break;
        case OP1:
            (void)bus_read(emu, emu->cpu.ea);
            advance_state(&emu->cpu, OP2, true);
            break;
        case OP2:
            putvalue(emu, emu->cpu.result);
            advance_state(&emu->cpu, OPCODE, true);
            break;
        default:
            break;
    }
}

static void rti(cbemu_t emu)
{
    switch(emu->cpu.op_state)
    {
        case OP0:
            advance_state(&emu->cpu, OP1, true);
            break;
        case OP1:
            (void)bus_read(emu, BASE_STACK+emu->cpu.regs.sp);
            advance_state(&emu->cpu, OP2, true);
            break;
        case OP2:
            emu->cpu.regs.status = pull8(emu);
            advance_state(&emu->cpu, OP3, true);
            break;
        case OP3:
            emu->cpu.tmpval = pull8(emu);
            advance_state(&emu->cpu, OP4, true);
            break;
        case OP4:
            emu->cpu.tmpval |= ((uint16_t)pull8(emu)) << 8;
            emu->cpu.regs.pc = emu->cpu.tmpval;
            advance_state(&emu->cpu, OPCODE, true);
            break;
        default:
            break;
    }
}

static void rts(cbemu_t emu)
{
    switch(emu->cpu.op_state)
    {
        case OP0:
            advance_state(&emu->cpu, OP1, true);
            break;
        case OP1:
            (void)bus_read(emu, BASE_STACK+emu->cpu.regs.sp);
            advance_state(&emu->cpu, OP2, true);
            break;
        case OP2:
            emu->cpu.tmpval = pull8(emu);
            advance_state(&emu->cpu, OP3, true);
            break;
        case OP3:
            emu->cpu.tmpval |= (uint16_t)pull8(emu) << 8;
            emu->cpu.regs.pc = emu->cpu.tmpval;
            advance_state(&emu->cpu, OP4, true);
            break;
        case OP4:
            /* Read the last PC of the JSR, then increment to the next op. */
            (void)bus_read(emu, emu->cpu.regs.pc++);
            advance_state(&emu->cpu, OPCODE, true);
            break;
        default:
            break;
    }
}

static void sbc(cbemu_t emu)
{
    uint16_t value;

    if(emu->cpu.op_state == OP0)
    {
        value = getvalue(emu) ^ 0x00FF;
        emu->cpu.result = (uint16_t)emu->cpu.regs.a + value + (uint16_t)(emu->cpu.regs.status & FLAG_CARRY);

        carrycalc(emu->cpu.regs.status, emu->cpu.result);
        zerocalc(emu->cpu.regs.status, emu->cpu.result);
        overflowcalc(emu->cpu.regs.status, emu->cpu.result, emu->cpu.regs.a, value);
        signcalc(emu->cpu.regs.status, emu->cpu.result);

        if(emu->cpu.regs.status & FLAG_DECIMAL)
        {
            advance_state(&emu->cpu, OP1, true);
        }
        else
        {
            emu->cpu.regs.a = emu->cpu.result;
            advance_state(&emu->cpu, OPCODE, true);
        }
    }
    else
    {
        clearcarry(emu->cpu.regs.status);

        emu->cpu.regs.a -= 0x66;
        if((emu->cpu.regs.a & 0x0F) > 0x09)
        {
            emu->cpu.regs.a += 0x06;
        }
        if((emu->cpu.regs.a & 0xF0) > 0x90)
        {
            emu->cpu.regs.a += 0x60;
            setcarry(emu->cpu.regs.status);
        }

        advance_state(&emu->cpu, OPCODE, true);
    }
}

static void sec(cbemu_t emu)
{
    setcarry(emu->cpu.regs.status);
    advance_state(&emu->cpu, OPCODE, true);
}

static void sed(cbemu_t emu)
{
    setdecimal(emu->cpu.regs.status);
    advance_state(&emu->cpu, OPCODE, true);
}

static void sei(cbemu_t emu)
{
    setinterrupt(emu->cpu.regs.status);
    advance_state(&emu->cpu, OPCODE, true);
}

static void sta(cbemu_t emu)
{
    cpu_addr_mode_t mode;

    if(emu->cpu.op_state == OP0)
    {
        mode = addrtable[emu->cpu.opcode];

        /* Store abs,X/Y instructions take an extra cycle regardless of page
         * crossing. However, if a page crossing occurred, this cycle has already
         * been handled by the address mode handler.
         */
        if((mode == ABSX || mode == ABSY) && !emu->cpu.page_boundary)
        {
            /* The extra cycle reads the eventual write address. */
            (void)bus_read(emu, emu->cpu.ea);
            advance_state(&emu->cpu, OP1, true);
        }
        else if(mode == INDY && !emu->cpu.page_boundary)
        {
            /* Another special penalty op. This team, re-read the second zp
             * address. This shoudl still be in emu->cpu.tmpval. */
            (void)bus_read(emu, emu->cpu.tmpval);
            advance_state(&emu->cpu, OP1, true);
        }
        else
        {
            putvalue(emu, emu->cpu.regs.a);
            advance_state(&emu->cpu, OPCODE, true);
        }
    }
    else
    {
        putvalue(emu, emu->cpu.regs.a);
        advance_state(&emu->cpu, OPCODE, true);
    }
}

static void stx(cbemu_t emu)
{
    cpu_addr_mode_t mode;

    if(emu->cpu.op_state == OP0)
    {
        mode = addrtable[emu->cpu.opcode];

        /* Store abs,X/Y instructions take an extra cycle regardless of page
         * crossing. However, if a page crossing occurred, this cycle has already
         * been handled by the address mode handler.
         */
        if((mode == ABSX || mode == ABSY) && !emu->cpu.page_boundary)
        {
            /* The extra cycle reads the eventual write address. */
            (void)bus_read(emu, emu->cpu.ea);
            advance_state(&emu->cpu, OP1, true);
        }
        else
        {
            putvalue(emu, emu->cpu.regs.x);
            advance_state(&emu->cpu, OPCODE, true);
        }
    }
    else
    {
        putvalue(emu, emu->cpu.regs.x);
        advance_state(&emu->cpu, OPCODE, true);
    }
}

static void sty(cbemu_t emu)
{
    cpu_addr_mode_t mode;

    if(emu->cpu.op_state == OP0)
    {
        mode = addrtable[emu->cpu.opcode];

        /* Store abs,X/Y instructions take an extra cycle regardless of page
         * crossing. However, if a page crossing occurred, this cycle has already
         * been handled by the address mode handler.
         */
        if((mode == ABSX || mode == ABSY) && !emu->cpu.page_boundary)
        {
            /* The extra cycle reads the eventual write address. */
            (void)bus_read(emu, emu->cpu.ea);
            advance_state(&emu->cpu, OP1, true);
        }
        else
        {
            putvalue(emu, emu->cpu.regs.y);
            advance_state(&emu->cpu, OPCODE, true);
        }
    }
    else
    {
        putvalue(emu, emu->cpu.regs.y);
        advance_state(&emu->cpu, OPCODE, true);
    }
}

static void stz(cbemu_t emu)
{
    cpu_addr_mode_t mode;

    if(emu->cpu.op_state == OP0)
    {
        mode = addrtable[emu->cpu.opcode];

        /* Store abs,X/Y instructions take an extra cycle regardless of page
         * crossing. However, if a page crossing occurred, this cycle has already
         * been handled by the address mode handler.
         */
        if((mode == ABSX || mode == ABSY) && !emu->cpu.page_boundary)
        {
            /* The extra cycle reads the eventual write address. */
            (void)bus_read(emu, emu->cpu.ea);
            advance_state(&emu->cpu, OP1, true);
        }
        else
        {
            putvalue(emu, 0);
            advance_state(&emu->cpu, OPCODE, true);
        }
    }
    else
    {
        putvalue(emu, 0);
        advance_state(&emu->cpu, OPCODE, true);
    }
}

static void tax(cbemu_t emu)
{
    emu->cpu.regs.x = emu->cpu.regs.a;

    zerocalc(emu->cpu.regs.status, emu->cpu.regs.x);
    signcalc(emu->cpu.regs.status, emu->cpu.regs.x);
    advance_state(&emu->cpu, OPCODE, true);
}

static void tay(cbemu_t emu)
{
    emu->cpu.regs.y = emu->cpu.regs.a;

    zerocalc(emu->cpu.regs.status, emu->cpu.regs.y);
    signcalc(emu->cpu.regs.status, emu->cpu.regs.y);
    advance_state(&emu->cpu, OPCODE, true);
}

static void tsx(cbemu_t emu)
{
    emu->cpu.regs.x = emu->cpu.regs.sp;

    zerocalc(emu->cpu.regs.status, emu->cpu.regs.x);
    signcalc(emu->cpu.regs.status, emu->cpu.regs.x);
    advance_state(&emu->cpu, OPCODE, true);
}

static void txa(cbemu_t emu)
{
    emu->cpu.regs.a = emu->cpu.regs.x;

    zerocalc(emu->cpu.regs.status, emu->cpu.regs.a);
    signcalc(emu->cpu.regs.status, emu->cpu.regs.a);
    advance_state(&emu->cpu, OPCODE, true);
}

static void txs(cbemu_t emu)
{
    emu->cpu.regs.sp = emu->cpu.regs.x;
    advance_state(&emu->cpu, OPCODE, true);
}

static void tya(cbemu_t emu)
{
    emu->cpu.regs.a = emu->cpu.regs.y;

    zerocalc(emu->cpu.regs.status, emu->cpu.regs.a);
    signcalc(emu->cpu.regs.status, emu->cpu.regs.a);
    advance_state(&emu->cpu, OPCODE, true);
}

static void wai(cbemu_t emu)
{
    //TODO
    advance_state(&emu->cpu, OPCODE, true);
}

static void trb(cbemu_t emu)
{
    uint8_t test;
    uint16_t value;

    switch(emu->cpu.op_state)
    {
        case OP0:
            value = getvalue(emu);

            test = value & emu->cpu.regs.a;
            zerocalc(emu->cpu.regs.status, test);

            emu->cpu.result = value & ~emu->cpu.regs.a;
            advance_state(&emu->cpu, OP1, true);
            break;
        case OP1:
            (void)bus_read(emu, emu->cpu.ea);
            advance_state(&emu->cpu, OP2, true);
            break;
        case OP2:
            putvalue(emu, emu->cpu.result);
            advance_state(&emu->cpu, OPCODE, true);
            break;
        default:
            break;
    }
}

static void tsb(cbemu_t emu)
{
    uint8_t test;
    uint16_t value;

    switch(emu->cpu.op_state)
    {
        case OP0:
            value = getvalue(emu);

            test = value & emu->cpu.regs.a;
            zerocalc(emu->cpu.regs.status, test);

            emu->cpu.result = value | emu->cpu.regs.a;
            advance_state(&emu->cpu, OP1, true);
            break;
        case OP1:
            (void)bus_read(emu, emu->cpu.ea);
            advance_state(&emu->cpu, OP2, true);
            break;
        case OP2:
            putvalue(emu, emu->cpu.result);
            advance_state(&emu->cpu, OPCODE, true);
            break;
        default:
            break;
    }
}

static void rmb(cbemu_t emu)
{
    uint8_t bit;
    uint16_t value;

    switch(emu->cpu.op_state)
    {
        case OP0:
            /* RMBX = 0x[0-7]7, so extract the bit to reset from the opcode. */
            bit = (emu->cpu.opcode >> 8);
            value = getvalue(emu);
            emu->cpu.result = value & ~(1 << bit);

            advance_state(&emu->cpu, OP1, true);
            break;
        case OP1:
            (void)bus_read(emu, emu->cpu.ea);
            advance_state(&emu->cpu, OP2, true);
            break;
        case OP2:
            putvalue(emu, emu->cpu.result);
            advance_state(&emu->cpu, OPCODE, true);
            break;
        default:
            break;
    }
}

static void smb(cbemu_t emu)
{
    uint8_t bit;
    uint16_t value;

    switch(emu->cpu.op_state)
    {
        case OP0:
            /* RMBX = 0x[0-7]7, so extract the bit to reset from the opcode. */
            bit = (emu->cpu.opcode >> 8);
            value = getvalue(emu);
            emu->cpu.result = value | (1 << bit);

            advance_state(&emu->cpu, OP1, true);
            break;
        case OP1:
            (void)bus_read(emu, emu->cpu.ea);
            advance_state(&emu->cpu, OP2, true);
            break;
        case OP2:
            putvalue(emu, emu->cpu.result);
            advance_state(&emu->cpu, OPCODE, true);
            break;
        default:
            break;
    }
}

static void bbr(cbemu_t emu)
{
    uint8_t bit;

    switch(emu->cpu.op_state)
    {
        case OP0:
            bit = (emu->cpu.opcode >> 8);

            /* The value has been cached here by the address mode handler. */
            if((emu->cpu.value && (1 << bit)) == 0)
            {
                advance_state(&emu->cpu, OP1, true);
            }
            else
            {
                advance_state(&emu->cpu, OPCODE, true);
            }
            break;
        case OP1:
            (void)bus_read(emu, emu->cpu.regs.pc);

            if((emu->cpu.regs.pc & 0xFF00) != ((emu->cpu.regs.pc + emu->cpu.reladdr) & 0xFF00))
            {
                /* Branch jumps a page, so we need to eat another cycle (PC
                 * again stays the same. */
                advance_state(&emu->cpu, OP2, true);
            }
            else
            {
                emu->cpu.regs.pc += emu->cpu.reladdr;
                advance_state(&emu->cpu, OPCODE, true);
            }
            break;
        case OP2:
            (void)bus_read(emu, emu->cpu.regs.pc);
            emu->cpu.regs.pc += emu->cpu.reladdr;
            advance_state(&emu->cpu, OPCODE, true);
            break;
        default:
            break;
    }
}

static void bbs(cbemu_t emu)
{
    uint8_t bit;

    switch(emu->cpu.op_state)
    {
        case OP0:
            bit = (emu->cpu.opcode >> 8);

            /* The value has been cached here by the address mode handler. */
            if((emu->cpu.value && (1 << bit)) != 0)
            {
                advance_state(&emu->cpu, OP1, true);
            }
            else
            {
                advance_state(&emu->cpu, OPCODE, true);
            }
            break;
        case OP1:
            (void)bus_read(emu, emu->cpu.regs.pc);

            if((emu->cpu.regs.pc & 0xFF00) != ((emu->cpu.regs.pc + emu->cpu.reladdr) & 0xFF00))
            {
                /* Branch jumps a page, so we need to eat another cycle (PC
                 * again stays the same. */
                advance_state(&emu->cpu, OP2, true);
            }
            else
            {
                emu->cpu.regs.pc += emu->cpu.reladdr;
                advance_state(&emu->cpu, OPCODE, true);
            }
            break;
        case OP2:
            (void)bus_read(emu, emu->cpu.regs.pc);
            emu->cpu.regs.pc += emu->cpu.reladdr;
            advance_state(&emu->cpu, OPCODE, true);
            break;
        default:
            break;
    }
}

static void vector(cbemu_t emu)
{
    switch(emu->cpu.op_state)
    {
        case VEC0:
            (void)bus_read(emu, emu->cpu.regs.pc);
            advance_state(&emu->cpu, VEC1, true);
            break;
        case VEC1:
            (void)bus_read(emu, emu->cpu.regs.pc);
            advance_state(&emu->cpu, VEC2, true);
            break;
        case VEC2:
            push8(emu, (emu->cpu.regs.pc >> 8) & 0xff);
            advance_state(&emu->cpu, VEC3, true);
            break;
        case VEC3:
            push8(emu, emu->cpu.regs.pc & 0xff);
            advance_state(&emu->cpu, VEC4, true);
            break;
        case VEC4:
            if(emu->cpu.vec_src == BRK_VEC)
            {
                push8(emu, emu->cpu.regs.status | FLAG_BREAK);
            }
            else
            {
                push8(emu, emu->cpu.regs.status);
            }

            if(emu->cpu.vec_src == RST_VEC)
            {
                /* W65C02 sets B clears D on reset. */
                cleardecimal(emu->cpu.regs.status);
            }

            emu->cpu.regs.status |= FLAG_INTERRUPT;

            advance_state(&emu->cpu, VEC5, true);
            break;
        case VEC5:
            switch(emu->cpu.vec_src)
            {
                case BRK_VEC:
                case IRQ_VEC:
                    emu->cpu.tmpval = 0xfffe;
                    break;
                case NMI_VEC:
                    emu->cpu.tmpval = 0xfffa;
                    break;
                case RST_VEC:
                    emu->cpu.tmpval = 0xfffc;
                    break;
                default:
                    break;
            }
            emu->cpu.ea = bus_read(emu, emu->cpu.tmpval);
            advance_state(&emu->cpu, VEC6, true);
            break;
        case VEC6:
            emu->cpu.ea |= (uint16_t)bus_read(emu, emu->cpu.tmpval+1) << 8;
            emu->cpu.regs.pc = emu->cpu.ea;
            advance_state(&emu->cpu, OPCODE, true);
            break;
        default:
            break;
    }
}

static void (*addr_handlers[NUM_ADDR_MODES])(cbemu_t emu) =
{
    imp,
    acc,
    imm,
    zp,
    zpx,
    zpy,
    rel,
    abso,
    absx,
    absy,
    ind,
    indx,
    indy,
    indz,
    abin,
    zprel,
};

static uint8_t addr_lengths[NUM_ADDR_MODES] =
{
    1,
    1,
    2,
    2,
    2,
    2,
    2,
    3,
    3,
    3,
    3,
    2,
    2,
    2,
    3,
    3
};

const cpu_addr_mode_t addrtable[256] =
{
/*        |  0  |  1  |  2  |  3  |  4  |  5  |  6  |  7  |  8  |  9  |  A  |  B  |  C  |  D  |  E  |  F  |       */
/* 0 */     IMP, INDX,  IMP, INDX,   ZP,   ZP,   ZP,    ZP,  IMP,  IMM,  ACC,  IMM, ABSO, ABSO, ABSO, ZPREL, /* 0 */
/* 1 */     REL, INDY, INDZ, INDY,   ZP,  ZPX,  ZPX,    ZP,  IMP, ABSY,  ACC, ABSY, ABSO, ABSX, ABSX, ZPREL, /* 1 */
/* 2 */    ABSO, INDX,  IMP, INDX,   ZP,   ZP,   ZP,    ZP,  IMP,  IMM,  ACC,  IMM, ABSO, ABSO, ABSO, ZPREL, /* 2 */
/* 3 */     REL, INDY, INDZ, INDY,  ZPX,  ZPX,  ZPX,    ZP,  IMP, ABSY,  ACC, ABSY, ABSX, ABSX, ABSX, ZPREL, /* 3 */
/* 4 */     IMP, INDX,  IMP, INDX,   ZP,   ZP,   ZP,    ZP,  IMP,  IMM,  ACC,  IMM, ABSO, ABSO, ABSO, ZPREL, /* 4 */
/* 5 */     REL, INDY, INDZ, INDY,  ZPX,  ZPX,  ZPX,    ZP,  IMP, ABSY,  IMP, ABSY, ABSX, ABSX, ABSX, ZPREL, /* 5 */
/* 6 */     IMP, INDX,  IMP, INDX,   ZP,   ZP,   ZP,    ZP,  IMP,  IMM,  ACC,  IMM,  IND, ABSO, ABSO, ZPREL, /* 6 */
/* 7 */     REL, INDY, INDZ, INDY,  ZPX,  ZPX,  ZPX,    ZP,  IMP, ABSY,  IMP, ABSY, ABIN, ABSX, ABSX, ZPREL, /* 7 */
/* 8 */     REL, INDX,  IMM, INDX,   ZP,   ZP,   ZP,    ZP,  IMP,  IMM,  IMP,  IMM, ABSO, ABSO, ABSO, ZPREL, /* 8 */
/* 9 */     REL, INDY, INDZ, INDY,  ZPX,  ZPX,  ZPY,    ZP,  IMP, ABSY,  IMP, ABSY, ABSO, ABSX, ABSX, ZPREL, /* 9 */
/* A */     IMM, INDX,  IMM, INDX,   ZP,   ZP,   ZP,    ZP,  IMP,  IMM,  IMP,  IMM, ABSO, ABSO, ABSO, ZPREL, /* A */
/* B */     REL, INDY, INDZ, INDY,  ZPX,  ZPX,  ZPY,    ZP,  IMP, ABSY,  IMP, ABSY, ABSX, ABSX, ABSY, ZPREL, /* B */
/* C */     IMM, INDX,  IMM, INDX,   ZP,   ZP,   ZP,    ZP,  IMP,  IMM,  IMP,  IMP, ABSO, ABSO, ABSO, ZPREL, /* C */
/* D */     REL, INDY, INDZ, INDY,  ZPX,  ZPX,  ZPX,    ZP,  IMP, ABSY,  IMP, ABSY, ABSX, ABSX, ABSX, ZPREL, /* D */
/* E */     IMM, INDX,  IMM, INDX,   ZP,   ZP,   ZP,    ZP,  IMP,  IMM,  IMP,  IMM, ABSO, ABSO, ABSO, ZPREL, /* E */
/* F */     REL, INDY, INDZ, INDY,  ZPX,  ZPX,  ZPX,    ZP,  IMP, ABSY,  IMP, ABSY, ABSX, ABSX, ABSX, ZPREL, /* F */
};

static void (*optable[256])(cbemu_t) =
{
/*        |  0  |  1  |  2  |  3  |  4  |  5  |  6  |  7  |  8  |  9  |  A  |  B  |  C  |  D  |  E  |  F  |      */
/* 0 */      brk,  ora,  nop,  nop,  tsb,  ora,  asl,  rmb,  ph_,  ora,  asl,  nop,  tsb,  ora,  asl,  bbr, /* 0 */
/* 1 */      bxx,  ora,  ora,  nop,  trb,  ora,  asl,  rmb,  clc,  ora,  inc,  nop,  trb,  ora,  asl,  bbr, /* 1 */
/* 2 */      jsr,  and,  nop,  nop,  bit,  and,  rol,  rmb,  pl_,  and,  rol,  nop,  bit,  and,  rol,  bbr, /* 2 */
/* 3 */      bxx,  and,  and,  nop,  bit,  and,  rol,  rmb,  sec,  and,  dec,  nop,  bit,  and,  rol,  bbr, /* 3 */
/* 4 */      rti,  eor,  nop,  nop,  nop,  eor,  lsr,  rmb,  ph_,  eor,  lsr,  nop,  jmp,  eor,  lsr,  bbr, /* 4 */
/* 5 */      bxx,  eor,  eor,  nop,  nop,  eor,  lsr,  rmb,  cli,  eor,  ph_,  nop,  nop,  eor,  lsr,  bbr, /* 5 */
/* 6 */      rts,  adc,  nop,  nop,  stz,  adc,  ror,  rmb,  pl_,  adc,  ror,  nop,  jmp,  adc,  ror,  bbr, /* 6 */
/* 7 */      bxx,  adc,  adc,  nop,  stz,  adc,  ror,  rmb,  sei,  adc,  pl_,  nop,  jmp,  adc,  ror,  bbr, /* 7 */
/* 8 */      bra,  sta,  nop,  nop,  sty,  sta,  stx,  smb,  dey,  bit,  txa,  nop,  sty,  sta,  stx,  bbs, /* 8 */
/* 9 */      bxx,  sta,  sta,  nop,  sty,  sta,  stx,  smb,  tya,  sta,  txs,  nop,  stz,  sta,  stz,  bbs, /* 9 */
/* A */      ldy,  lda,  ldx,  nop,  ldy,  lda,  ldx,  smb,  tay,  lda,  tax,  nop,  ldy,  lda,  ldx,  bbs, /* A */
/* B */      bxx,  lda,  lda,  nop,  ldy,  lda,  ldx,  smb,  clv,  lda,  tsx,  nop,  ldy,  lda,  ldx,  bbs, /* B */
/* C */      cpy,  cmp,  nop,  nop,  cpy,  cmp,  dec,  smb,  iny,  cmp,  dex,  wai,  cpy,  cmp,  dec,  bbs, /* C */
/* D */      bxx,  cmp,  cmp,  nop,  nop,  cmp,  dec,  smb,  cld,  cmp,  ph_,  nop,  nop,  cmp,  dec,  bbs, /* D */
/* E */      cpx,  sbc,  nop,  nop,  cpx,  sbc,  inc,  smb,  inx,  sbc,  nop,  nop,  cpx,  sbc,  inc,  bbs, /* E */
/* F */      bxx,  sbc,  sbc,  nop,  nop,  sbc,  inc,  smb,  sed,  sbc,  pl_,  nop,  nop,  sbc,  inc,  bbs  /* F */
};

static const uint32_t ticktable[256] =
{
/* 0 */      7,    6,    2,    8,    5,    3,    5,    5,    3,    2,    2,    2,    6,    4,    6,    5,  /* 0 */
/* 1 */      2,    5,    5,    8,    5,    4,    6,    6,    2,    4,    2,    7,    6,    4,    7,    5,  /* 1 */
/* 2 */      6,    6,    2,    8,    3,    3,    5,    5,    4,    2,    2,    2,    4,    4,    6,    5,  /* 2 */
/* 3 */      2,    5,    5,    8,    4,    4,    6,    6,    2,    4,    2,    7,    4,    4,    7,    5,  /* 3 */
/* 4 */      6,    6,    2,    8,    3,    3,    5,    5,    3,    2,    2,    2,    3,    4,    6,    5,  /* 4 */
/* 5 */      2,    5,    2,    8,    4,    4,    6,    6,    2,    4,    3,    7,    4,    4,    7,    5,  /* 5 */
/* 6 */      6,    6,    2,    8,    3,    3,    5,    5,    4,    2,    2,    2,    5,    4,    6,    5,  /* 6 */
/* 7 */      2,    5,    2,    8,    4,    4,    6,    6,    2,    4,    4,    7,    4,    4,    7,    5,  /* 7 */
/* 8 */      2,    6,    2,    6,    3,    3,    3,    3,    2,    2,    2,    2,    4,    4,    4,    5,  /* 8 */
/* 9 */      2,    6,    2,    6,    4,    4,    4,    4,    2,    5,    2,    5,    4,    5,    5,    5,  /* 9 */
/* A */      2,    6,    2,    6,    3,    3,    3,    3,    2,    2,    2,    2,    4,    4,    4,    5,  /* A */
/* B */      2,    5,    5,    5,    4,    4,    4,    4,    2,    4,    2,    4,    4,    4,    4,    5,  /* B */
/* C */      2,    6,    2,    8,    3,    3,    5,    5,    2,    2,    2,    2,    4,    4,    6,    5,  /* C */
/* D */      2,    5,    2,    8,    4,    4,    6,    6,    2,    4,    3,    7,    4,    4,    7,    5,  /* D */
/* E */      2,    6,    2,    8,    3,    3,    5,    5,    2,    2,    2,    2,    4,    4,    6,    5,  /* E */
/* F */      2,    5,    2,    8,    4,    4,    6,    6,    2,    4,    4,    7,    4,    4,    7,    5   /* F */
};

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

/**
 * Initialize the 6502 emulator. This sets the memory access functions as well as performs
 * an optional initial reset6502().
 *
 * @param system_cxt    Handle of the global system context. The CPU uses this to access the
 *                      memory space as well as check the status of the interrupt vectors.
 * @param reset         Perform a reset6502 as well as initialization.
 */
bool cpu_init(cbemu_t emu)
{
    memset(&emu->cpu, 0, sizeof(emu->cpu));
    emu->cpu.regs.status |= FLAG_CONSTANT;
    emu->cpu.init = true;

    /* Start in the reset vector state. */
    emu->cpu.op_state = VEC0;
    emu->cpu.vec_src = RST_VEC;

    return true;
}

void cpu_tick(cbemu_t emu)
{
    emu->cpu.cycle_consumed = false;

    while(!emu->cpu.cycle_consumed)
    {
        if(emu->cpu.op_state == OPCODE)
        {
            if(emu->cpu.nmi_edge)
            {
                emu->cpu.nmi_edge = false;
                emu->cpu.vec_src = NMI_VEC;
                emu->cpu.op_state = VEC0;
            }
            else if((emu->cpu.irq_votes > 0) && !(emu->cpu.regs.status & FLAG_INTERRUPT))
            {
                emu->cpu.vec_src = IRQ_VEC;
                emu->cpu.op_state = VEC0;
            }
            else
            {
                emu->cpu.opcode = bus_read(emu, emu->cpu.regs.pc++);
                emu->cpu.cycle_consumed = true;
                emu->cpu.op_state = PARAM0;
            }
        }
        else if(emu->cpu.op_state < OP0)
        {
            (*addr_handlers[addrtable[emu->cpu.opcode]])(emu);
        }
        else if(emu->cpu.op_state < VEC0)
        {
            (*optable[emu->cpu.opcode])(emu);
        }
        else
        {
            vector(emu);
        }
    }

    /* TODO here is up a level? */
    clock_main_tick(&emu->clk);

    emu->cpu.cycle_consumed = false;
}

uint8_t cpu_step(cbemu_t emu)
{
    uint32_t elapsed = 0;

    /* If we are synced on the start the next opcode, go ahead and
     * tick once to read the opcode. Otherwise, skip this and just
     * loop until the current op is finished. */
    if(emu->cpu.op_state == OPCODE)
    {
        cpu_tick(emu);
        ++elapsed;
    }

    while(emu->cpu.op_state != OPCODE)
    {
        cpu_tick(emu);
        ++elapsed;
    }

    return (uint8_t)elapsed;
}
#if 0

void cpu_disassemble(size_t bufLen, char *buffer)
{
    cpu_disassemble_at(emu->cpu.regs.pc, bufLen, buffer);
}

void cpu_disassemble_at(uint16_t addr, size_t buf_len, char *buffer)
{
    const char *mn;
    cpu_addr_mode_t addr_mode;
    uint8_t opcode = sys_peek_mem(syscxt, addr);
    size_t len;
    mn = mnemonics[opcode];
    addr_mode = addrtable[opcode];
    snprintf(buffer, buf_len, "0x%04x: %s", addr, mn);

    len = strlen(buffer);

    buffer += len;
    buf_len -= len;

    if(buf_len == 0)
        return;

    switch(addr_mode)
    {
        case IMM:
            snprintf(buffer, buf_len, " #$%02x", sys_peek_mem(syscxt, addr+1));
            break;
        case ZP:
            snprintf(buffer, buf_len, " $00%02x", sys_peek_mem(syscxt, addr+1));
            break;
        case ZPX:
            snprintf(buffer, buf_len, " $00%02x,X", sys_peek_mem(syscxt, addr+1));
            break;
        case ZPY:
            snprintf(buffer, buf_len, " $00%02x,Y", sys_peek_mem(syscxt, addr+1));
            break;
        case REL:
            snprintf(buffer, buf_len, " $%02x", sys_peek_mem(syscxt, addr+1));
            break;
        case ABSO:
            snprintf(buffer, buf_len, " $%02x%02x", sys_peek_mem(syscxt, addr+2), sys_peek_mem(syscxt, addr+1));
            break;
        case ABSX:
            snprintf(buffer, buf_len, " $%02x%02x,X", sys_peek_mem(syscxt, addr+2), sys_peek_mem(syscxt, addr+1));
            break;
        case ABSY:
            snprintf(buffer, buf_len, " $%02x%02x,Y", sys_peek_mem(syscxt, addr+2), sys_peek_mem(syscxt, addr+1));
            break;
        case IND:
            snprintf(buffer, buf_len, " ($%02x%02x)", sys_peek_mem(syscxt, addr+2), sys_peek_mem(syscxt, addr+1));
            break;
        case INDX:
            snprintf(buffer, buf_len, " ($00%02x,X)", sys_peek_mem(syscxt, addr+1));
            break;
        case INDY:
            snprintf(buffer, buf_len, " ($00%02x,Y)", sys_peek_mem(syscxt, addr+1));
            break;
        case INDZ:
            snprintf(buffer, buf_len, " ($00%02x)", sys_peek_mem(syscxt, addr+1));
            break;
        default:
            break;
    }
}

uint16_t cpu_get_reg(cpu_reg_t reg)
{
    uint16_t val;

    switch(reg)
    {
        case REG_A:
            val = a;
            break;
        case REG_X:
            val = x;
            break;
        case REG_Y:
            val = y;
            break;
        case REG_PC:
            val = pc;
            break;
        case REG_SP:
            val = sp;
            break;
        case REG_S:
            val = status;
            break;
        default:
            val = 0xffff;
            break;
    }

    return val;
}

void cpu_get_regs(cpu_regs_t *regs)
{
    if(regs != NULL)
    {
        regs->a = a;
        regs->x = x;
        regs->y = y;
        regs->sp = sp;
        regs->pc = pc;
        regs->s = status;
    }
}

uint8_t cpu_get_op_len(void)
{
    return cpu_get_op_len_at(pc);
}

uint8_t cpu_get_op_len_at(uint16_t addr)
{
    uint8_t opcode = sys_peek_mem(syscxt, addr);
    return addr_lengths[addrtable[opcode]];
}

bool cpu_is_subroutine(void)
{
    uint8_t opcode = sys_peek_mem(syscxt, pc);

    return opcode == 0x20;
}

void cpu_set_tick_callback(cpu_tick_cb_t callback)
{
    tick_callback = callback;
}
#endif


uint16_t cpu_get_pc(cbemu_t emu)
{
    return emu->cpu.regs.pc;
}

bool cpu_is_sync(cbemu_t emu)
{
    return emu->cpu.op_state == OPCODE;
}
