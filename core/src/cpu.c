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
#include "cpu.h"
#include "sys.h"

#define FLAG_CARRY     0x01
#define FLAG_ZERO      0x02
#define FLAG_INTERRUPT 0x04
#define FLAG_DECIMAL   0x08
#define FLAG_BREAK     0x10
#define FLAG_CONSTANT  0x20
#define FLAG_OVERFLOW  0x40
#define FLAG_SIGN      0x80

#define BASE_STACK     0x100

#define saveaccum(n) a = (uint8_t)((n) & 0x00FF)


//flag modifier macros
#define setcarry() status |= FLAG_CARRY
#define clearcarry() status &= (~FLAG_CARRY)
#define setzero() status |= FLAG_ZERO
#define clearzero() status &= (~FLAG_ZERO)
#define setinterrupt() status |= FLAG_INTERRUPT
#define clearinterrupt() status &= (~FLAG_INTERRUPT)
#define setdecimal() status |= FLAG_DECIMAL
#define cleardecimal() status &= (~FLAG_DECIMAL)
#define setoverflow() status |= FLAG_OVERFLOW
#define clearoverflow() status &= (~FLAG_OVERFLOW)
#define setsign() status |= FLAG_SIGN
#define clearsign() status &= (~FLAG_SIGN)


//flag calculation macros
#define zerocalc(n) \
    do { \
        if((n) & 0x00FF) \
            clearzero(); \
        else \
            setzero(); \
    } while(0)

#define signcalc(n) \
    do { \
        if((n) & 0x0080) \
            setsign(); \
        else \
            clearsign(); \
    } while(0)

#define carrycalc(n) \
    do { \
    if((n) & 0xFF00) \
        setcarry(); \
    else \
        clearcarry(); \
    } while(0)

/* n = result, m = accumulator, o = memory */
#define overflowcalc(n, m, o) \
    do { \
        if(((n) ^ (uint16_t)(m)) & ((n) ^ (o)) & 0x0080) \
            setoverflow(); \
        else \
            clearoverflow(); \
    } while(0)

typedef enum
{
    BRK_VEC,
    NMI_VEC,
    RST_VEC,
    IRQ_VEC
} cpu_vec_src_t;

//6502 CPU registers
static uint16_t pc;
static uint8_t sp, a, x, y, status;


//helper variables
static uint32_t instructions = 0; //keep track of total instructions executed
static uint16_t oldpc, ea, reladdr, value, result;
static uint8_t opcode, oldstatus;
static uint16_t tmpval;
static bool page_boundary, cycle_consumed;
static cpu_vec_src_t vec_src;

static sys_cxt_t syscxt;
static cpu_tick_cb_t tick_callback;

//a few general functions used by various other functions
void push16(uint16_t pushval)
{
    sys_write_mem(syscxt, BASE_STACK + sp, (pushval >> 8) & 0xFF);
    sys_write_mem(syscxt, BASE_STACK + ((sp - 1) & 0xFF), pushval & 0xFF);
    sp -= 2;
}

void push8(uint8_t pushval)
{
    sys_write_mem(syscxt, BASE_STACK + sp--, pushval);
}

uint16_t pull16()
{
    uint16_t temp16;
    temp16 = sys_read_mem(syscxt, BASE_STACK + ((sp + 1) & 0xFF)) | ((uint16_t)sys_read_mem(syscxt, BASE_STACK + ((sp + 2) & 0xFF)) << 8);
    sp += 2;
    return(temp16);
}

uint8_t pull8()
{
    return (sys_read_mem(syscxt, BASE_STACK + ++sp));
}

void cpu_reset()
{
    pc = (uint16_t)sys_read_mem(syscxt, 0xFFFC) | ((uint16_t)sys_read_mem(syscxt, 0xFFFD) << 8);
    a = 0;
    x = 0;
    y = 0;
    sp = 0xFD;
    status |= FLAG_CONSTANT;
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

const cpu_addr_mode_t addrtable[256];
static void (*optable[256])();
static uint8_t penaltyop, penaltyaddr;
static op_state_t op_state;
static const uint8_t branch_shift_map[] = {
    7, /* N */
    6, /* V */
    0, /* C */
    1  /* Z */
};



static inline void advance_state(op_state_t new_state, bool memcycle)
{
    cycle_consumed = memcycle;

    op_state = new_state;
}

//addressing mode functions, calculates effective addresses

//implied
static void imp()
{
    /* Implied opcodes take 2 cycles, reading the next PC an additional time. */
    (void)sys_read_mem(syscxt, pc);
    advance_state(OP0, false);
}

//accumulator
static void acc()
{
    /* Implied opcodes take 2 cycles, reading the next PC an additional time. */
    (void)sys_read_mem(syscxt, pc);
    advance_state(OP0, false);
}

//immediate
static void imm()
{
    ea = pc++;

    /* The read of the effective address will consume the second cycle. */
    advance_state(OP0, false);
}

//zero-page
static void zp()
{
    ea = (uint16_t)sys_read_mem(syscxt, (uint16_t)pc++);
    advance_state(OP0, true);
}

//zero-page,X
static void zpx()
{
    if(op_state == PARAM0)
    {
        ea = sys_read_mem(syscxt, pc);
        advance_state(PARAM1, true);
    }
    else
    {
        ea = (ea + (uint16_t)x) & 0x00FF;
        (void)sys_read_mem(syscxt, pc++);
        advance_state(OP0, true);
    }
}

//zero-page,Y
static void zpy()
{
    if(op_state == PARAM0)
    {
        ea = sys_read_mem(syscxt, pc);
        advance_state(PARAM1, true);
    }
    else
    {
        ea = (ea + (uint16_t)y) & 0x00FF;
        (void)sys_read_mem(syscxt, pc++);
        advance_state(OP0, true);
    }
}

//relative for branch ops (8-bit immediate value, sign-extended)
static void rel()
{
    reladdr = (uint16_t)sys_read_mem(syscxt, pc++);
    if(reladdr & 0x80)
        reladdr |= 0xFF00;

    advance_state(OP0, false);
}

//absolute
static void abso()
{
    if(opcode == 0x20)
    {
        /* JSR is abso-like in that it has a 2 byte absolute address
         * parameter. But it acts differently. Let it handle itself.
         */
        advance_state(OP0, false);
        return;
    }

    if(op_state == PARAM0)
    {
        ea = (uint16_t)sys_read_mem(syscxt, pc++);
        advance_state(PARAM1, true);
    }
    else
    {
        ea |= ((uint16_t)sys_read_mem(syscxt, pc++)) << 8;

        /* JMP a is special in that there is no cycle after the immediate
         * address read. For it specifically, do not consume a cycle here.
         */
        if(opcode == 0x4c)
        {
            advance_state(OP0, false);
        }
        else
        {
            advance_state(OP0, true);
        }
    }
}

//absolute,X
static void absx()
{
    uint16_t startpage;

    if(op_state == PARAM0)
    {
        ea = (uint16_t)sys_read_mem(syscxt, pc++);
        advance_state(PARAM1, true);
    }
    else if(op_state == PARAM1)
    {
        ea |= (uint16_t)sys_read_mem(syscxt, pc) << 8;

        startpage = ea & 0xFF00;
        ea += (uint16_t)x;

        if(startpage != (ea & 0xFF00))
        {
            /* Absolute address crossed pages. We need to eat one more penalty
             * cycle. */
            page_boundary = true;
            advance_state(PARAM2, true);
        }
        else
        {
            ++pc;
            advance_state(OP0, true);
        }
    }
    else
    {
        /* Penalty cycle. Repeat read cycle on current PC. */
        (void)sys_read_mem(syscxt, pc++);
        advance_state(OP0, true);
    }
}

//absolute,Y
static void absy()
{
    uint16_t startpage;

    if(op_state == PARAM0)
    {
        ea = (uint16_t)sys_read_mem(syscxt, pc++);
        advance_state(PARAM1, true);
    }
    else if(op_state == PARAM1)
    {
        ea |= (uint16_t)sys_read_mem(syscxt, pc) << 8;

        startpage = ea & 0xFF00;
        ea += (uint16_t)y;

        if(startpage != (ea & 0xFF00))
        {
            /* Absolute address crossed pages. We need to eat one more penalty
             * cycle. */
            page_boundary = true;
            advance_state(PARAM2, true);
        }
        else
        {
            ++pc;
            advance_state(OP0, true);
        }
    }
    else
    {
        /* Penalty cycle. Repeat read cycle on current PC. */
        (void)sys_read_mem(syscxt, pc++);
        advance_state(OP0, true);
    }
}

//indirect
static void ind()
{
    switch(op_state)
    {
        case PARAM0:
            tmpval = (uint16_t)sys_read_mem(syscxt, pc++);
            advance_state(PARAM1, true);
            break;
        case PARAM1:
            tmpval |= (uint16_t)sys_read_mem(syscxt, pc) << 8;
            advance_state(PARAM2, true);
            break;
        case PARAM2:
            /* 65C02 always takes an extra cycle here. This is the workaround to the
             * NMOS 6502 issue if page-wrap on this on this opcode. */
            (void)sys_read_mem(syscxt, pc);
            advance_state(PARAM3, true);
            break;
        case PARAM3:
            ea = (uint16_t)sys_read_mem(syscxt, tmpval);
            advance_state(PARAM4, true);
            break;
        case PARAM4:
            ea |= (uint16_t)sys_read_mem(syscxt, tmpval+1) << 8;
            advance_state(OP0, false);
            break;
        default:
            break;
    }
}

// (indirect,X)
static void indx()
{
    switch(op_state)
    {
        case PARAM0:
            tmpval = sys_read_mem(syscxt, pc);
            advance_state(PARAM1, true);
            break;
        case PARAM1:
            tmpval = (tmpval + (uint16_t)x) & 0x00FF;
            (void)sys_read_mem(syscxt, pc++);
            advance_state(PARAM2, true);
            break;
        case PARAM2:
            ea = sys_read_mem(syscxt, tmpval);
            tmpval = (tmpval + 1) & 0x00FF;
            advance_state(PARAM3, true);
            break;
        case PARAM3:
            ea |= (uint16_t)sys_read_mem(syscxt, tmpval) << 8;
            advance_state(OP0, true);
            break;
        default:
            break;
    }
}

// (indirect),Y
static void indy()
{
    uint16_t startpage;
    switch(op_state)
    {
        case PARAM0:
            tmpval = sys_read_mem(syscxt, pc++);
            advance_state(PARAM1, true);
            break;
        case PARAM1:
            ea = sys_read_mem(syscxt, tmpval);
            tmpval = (tmpval + 1) & 0x00FF;
            advance_state(PARAM2, true);
            break;
        case PARAM2:
            ea |= sys_read_mem(syscxt, tmpval) << 8;
            startpage = ea & 0xFF00;

            ea += (uint16_t)y;

            if(startpage != (ea & 0xFF00))
            {
                page_boundary = true;
                advance_state(PARAM3, true);
            }
            else
            {
                advance_state(OP0, true);
            }
            break;
        case PARAM3:
            /* Repeat the second zeropage read on page cross. */
            (void)sys_read_mem(syscxt, tmpval);
            advance_state(OP0, true);
            break;
        default:
            break;
    }
}

// (zp)
static void indz()
{
    switch(op_state)
    {
        case PARAM0:
            tmpval = sys_read_mem(syscxt, pc++);
            advance_state(PARAM1, true);
            break;
        case PARAM1:
            ea = sys_read_mem(syscxt, tmpval);
            tmpval = (tmpval + 1) & 0xFF;
            advance_state(PARAM2, true);
            break;
        case PARAM2:
            ea |= ((uint16_t)sys_read_mem(syscxt, tmpval)) << 8;
            advance_state(OP0, true);
            break;
        default:
            break;
    }
}

// (a,x)
static void abin()
{
    switch(op_state)
    {
        case PARAM0:
            tmpval = sys_read_mem(syscxt, pc++);
            advance_state(PARAM1, true);
            break;
        case PARAM1:
            /* Note: PC does not seem to increment after second imm read.
             *       The only opcode that uses this mode is jmp (a,x), so 
             *       the PC is overwritten anyways. */
            tmpval |= (uint16_t)sys_read_mem(syscxt, pc) << 8;
            advance_state(PARAM2, true);
            break;
        case PARAM2:
            /* Bus re-reads same byte. Presumably this is to add X, handle
             * carry for indexing. */
            (void)sys_read_mem(syscxt, pc);
            tmpval += (uint16_t)x;
            advance_state(PARAM3, true);
            break;
        case PARAM3:
            ea = sys_read_mem(syscxt, tmpval);
            advance_state(PARAM4, true);
            break;
        case PARAM4:
            ea |= (uint16_t)sys_read_mem(syscxt, tmpval+1) << 8;
            advance_state(OP0, false);
            break;
        default:
            break;
    }
}

static void zprel()
{
    switch(op_state)
    {
        case PARAM0:
            ea = sys_read_mem(syscxt, pc++);
            advance_state(PARAM1, true);
            break;
        case PARAM1:
            /* Read the value from zp before reading the relative branch. */
            value = sys_read_mem(syscxt, ea);
            advance_state(PARAM2, true);
            break;
        case PARAM2:
            /* TODO This needs verification. The opcode reads from the ea twice. The question is
             *      whether the second read is ignored or can actually affect the result if the
             *      value changes. For now, assume it is ignored.
             */
            (void)sys_read_mem(syscxt, ea);
            advance_state(PARAM3, true);
            break;
        case PARAM3:
            reladdr = sys_read_mem(syscxt, pc++);
            advance_state(OP0, false);
            break;
        default:
            break;
    }
}

static uint16_t getvalue()
{
    uint16_t value;

    if(addrtable[opcode] == ACC)
    {
        value = (uint16_t)a;
    }
    else
    {
        value = (uint16_t)sys_read_mem(syscxt, ea);
    }

    return value;
}

static uint16_t getvalue16()
{
    return((uint16_t)sys_read_mem(syscxt, ea) | ((uint16_t)sys_read_mem(syscxt, ea+1) << 8));
}

static void putvalue(uint16_t saveval)
{
    if(addrtable[opcode] == ACC)
    {
        a = (uint8_t)(saveval & 0x00FF);
    }
    else
    {
        sys_write_mem(syscxt, ea, (saveval & 0x00FF));
    }
}


//instruction handler functions
static void adc()
{
    if(op_state == OP0)
    {
        value = getvalue();

        result = (uint16_t)a + value + (uint16_t)(status & FLAG_CARRY);

        carrycalc(result);
        zerocalc(result);
        overflowcalc(result, a, value);
        signcalc(result);

        if(status & FLAG_DECIMAL)
        {
            advance_state(OP1, true);
        }
        else
        {
            saveaccum(result);
            advance_state(OPCODE, true);
        }
    }
    else
    {
        /* Only takes a second cycle for decimal mode. */
        clearcarry();

        if((a & 0x0F) > 0x09)
        {
            a += 0x06;
        }
        if((a & 0xF0) > 0x90)
        {
            a += 0x60;
            setcarry();
        }

        saveaccum(result);
        advance_state(OPCODE, true);
    }
}

static void and()
{
    value = getvalue();

    result = (uint16_t)a & value;

    zerocalc(result);
    signcalc(result);

    saveaccum(result);
    advance_state(OPCODE, true);
}

static void asl()
{
    switch(op_state)
    {
        case OP0:
            value = getvalue();

            result = value << 1;

            carrycalc(result);
            zerocalc(result);
            signcalc(result);

            if(addrtable[opcode] == ACC)
            {
                /* Accumulator, so no additional cycles to consume. */
                putvalue(result);
                advance_state(OPCODE, true);
            }
            else
            {
                advance_state(OP1, true);
            }
            break;
        case OP1:
            /* Re-read the ea. */
            (void)sys_read_mem(syscxt, ea);
            advance_state(OP2, true);
            break;
        case OP2:
            putvalue(result);
            advance_state(OPCODE, true);
            break;
        default:
            break;
    }
}

static void bxx()
{
    uint8_t exp_flag;
    uint8_t flag_shift;

    switch(op_state)
    {
        case OP0:
            /* The top 2 bits of the opcode indicate which flag is being checked, and bit 6
             * indicates whether the bit should be set. */
            flag_shift = branch_shift_map[(opcode & 0xc0) >> 6];
            exp_flag = (opcode & 0x20) >> 5;

            if(((status >> flag_shift) & 0x01) == exp_flag)
            {
                /* PC stays the same from the bus standpoint. */
                advance_state(OP1, true);
            }
            else
            {
                advance_state(OPCODE, true);
            }
            break;
        case OP1:
            (void)sys_read_mem(syscxt, pc);

            if((pc & 0xFF00) != ((pc + reladdr) & 0xFF00))
            {
                /* Branch jumps a page, so we need to eat another cycle (PC
                 * again stays the same. */
                advance_state(OP2, true);
            }
            else
            {
                pc += reladdr;
                advance_state(OPCODE, true);
            }
            break;
        case OP2:
            (void)sys_read_mem(syscxt, pc);
            pc += reladdr;
            advance_state(OPCODE, true);
            break;
        default:
            break;
    }
}

static void bit()
{
    value = getvalue();
    result = (uint16_t)a & value;

    zerocalc(result);
    status = (status & 0x3F) | (uint8_t)(value & 0xC0);

    advance_state(OPCODE, true);
}

static void bra()
{
    switch(op_state)
    {
        case OP0:
            /* Consume the relative addr read cycle. */
            advance_state(OP1, true);
            break;
        case OP1:
            (void)sys_read_mem(syscxt, pc);

            if((pc & 0xFF00) != ((pc + reladdr) & 0xFF00))
            {
                advance_state(OP2, true);
            }
            else
            {
                pc += reladdr;
                advance_state(OPCODE, true);
            }
            break;
        case OP2:
            (void)sys_read_mem(syscxt, pc);
            pc += reladdr;
            advance_state(OPCODE, true);
            break;
        default:
            break;
    }
}

static void brk()
{
    /* Consume the second PC read, then let the vector
     * state machine take over. */
    advance_state(VEC2, true);
    vec_src = BRK_VEC;
}

static void clc()
{
    clearcarry();
    advance_state(OPCODE, true);
}

static void cld()
{
    cleardecimal();
    advance_state(OPCODE, true);
}

static void cli()
{
    clearinterrupt();
    advance_state(OPCODE, true);
}

static void clv()
{
    clearoverflow();
    advance_state(OPCODE, true);
}

static void cmp()
{
    value = getvalue();
    result = (uint16_t)a - value;

    if(a >= (uint8_t)(value & 0x00FF))
        setcarry();
    else
        clearcarry();

    if(a == (uint8_t)(value & 0x00FF))
        setzero();
    else
        clearzero();

    signcalc(result);
    advance_state(OPCODE, true);
}

static void cpx()
{
    value = getvalue();
    result = (uint16_t)x - value;

    if(x >= (uint8_t)(value & 0x00FF))
        setcarry();
    else
        clearcarry();

    if(x == (uint8_t)(value & 0x00FF))
        setzero();
    else
        clearzero();

    signcalc(result);
    advance_state(OPCODE, true);
}

static void cpy()
{
    value = getvalue();
    result = (uint16_t)y - value;

    if(y >= (uint8_t)(value & 0x00FF))
        setcarry();
    else
        clearcarry();

    if(y == (uint8_t)(value & 0x00FF))
        setzero();
    else
        clearzero();

    signcalc(result);
    advance_state(OPCODE, true);
}

static void dec()
{
    switch(op_state)
    {
        case OP0:
            /* There is a fixed penalty cycle here for some addressing modes. For a page boundary,
             * it has already been consumed. If not, we need to consumed it here with an additonal
             * EA read. */
            if(addrtable[opcode] == ABSX && !page_boundary)
            {
                (void)sys_read_mem(syscxt, ea);
                advance_state(OP1, true);
            }
            else
            {
                /* Advance without consuming a cycle. */
                advance_state(OP1, false);
            }
            break;
        case OP1:
            value = getvalue();
            result = value - 1;

            zerocalc(result);
            signcalc(result);

            if(addrtable[opcode] == ACC)
            {
                putvalue(result);
                advance_state(OPCODE, true);
            }
            else
            {
                advance_state(OP2, true);
            }
            break;
        case OP2:
            (void)sys_read_mem(syscxt, ea);
            advance_state(OP3, true);
            break;
        case OP3:
            putvalue(result);
            advance_state(OPCODE, true);
            break;
        default:
            break;
    }
}

static void dex()
{
    x--;

    zerocalc(x);
    signcalc(x);

    advance_state(OPCODE, true);
}

static void dey()
{
    y--;

    zerocalc(y);
    signcalc(y);

    advance_state(OPCODE, true);
}

static void eor()
{
    penaltyop = 1;
    value = getvalue();
    result = (uint16_t)a ^ value;

    zerocalc(result);
    signcalc(result);

    saveaccum(result);
    advance_state(OPCODE, true);
}

static void inc()
{
    switch(op_state)
    {
        case OP0:
            /* There is a fixed penalty cycle here for some addressing modes. For a page boundary,
             * it has already been consumed. If not, we need to consumed it here with an additonal
             * EA read. */
            if(addrtable[opcode] == ABSX && !page_boundary)
            {
                (void)sys_read_mem(syscxt, ea);
                advance_state(OP1, true);
            }
            else
            {
                /* Advance without consuming a cycle. */
                advance_state(OP1, false);
            }
            break;
        case OP1:
            value = getvalue();
            result = value + 1;

            zerocalc(result);
            signcalc(result);

            if(addrtable[opcode] == ACC)
            {
                putvalue(result);
                advance_state(OPCODE, true);
            }
            else
                advance_state(OP2, true);
            break;
        case OP2:
            (void)sys_read_mem(syscxt, ea);
            advance_state(OP3, true);
            break;
        case OP3:
            putvalue(result);
            advance_state(OPCODE, true);
            break;
        default:
            break;
    }
}

static void inx()
{
    x++;

    zerocalc(x);
    signcalc(x);

    advance_state(OPCODE, true);
}

static void iny()
{
    y++;

    zerocalc(y);
    signcalc(y);

    advance_state(OPCODE, true);
}

static void jmp()
{
    pc = ea;
    advance_state(OPCODE, true);
}

static void jsr()
{
    switch(op_state)
    {
        case OP0:
            ea = sys_read_mem(syscxt, pc++);
            advance_state(OP1, true);
            break;
        case OP1:
            /* Ignored stack read. */
            (void)sys_read_mem(syscxt, BASE_STACK + sp);
            advance_state(OP2, true);
            break;
        case OP2:
            push8((pc & 0xFF00) >> 8);
            advance_state(OP3, true);
            break;
        case OP3:
            push8(pc & 0xFF);
            advance_state(OP4, true);
            break;
        case OP4:
            ea |= (uint16_t)sys_read_mem(syscxt, pc) << 8;
            pc = ea;
            advance_state(OPCODE, true);
            break;
        default:
            break;
    }
}

static void lda()
{
    penaltyop = 1;
    value = getvalue();
    a = (uint8_t)(value & 0x00FF);

    zerocalc(a);
    signcalc(a);
    advance_state(OPCODE, true);
}

static void ldx()
{
    penaltyop = 1;
    value = getvalue();
    x = (uint8_t)(value & 0x00FF);

    zerocalc(x);
    signcalc(x);
    advance_state(OPCODE, true);
}

static void ldy()
{
    penaltyop = 1;
    value = getvalue();
    y = (uint8_t)(value & 0x00FF);

    zerocalc(y);
    signcalc(y);
    advance_state(OPCODE, true);
}

static void lsr()
{
    switch(op_state)
    {
        case OP0:
            value = getvalue();
            result = value >> 1;

            if(value & 1)
                setcarry();
            else
                clearcarry();

            zerocalc(result);
            signcalc(result);

            if(addrtable[opcode] == ACC)
            {
                putvalue(result);
                advance_state(OPCODE, true);
            }
            else
            {
                advance_state(OP1, true);
            }

            break;
        case OP1:
            (void)sys_read_mem(syscxt, ea);
            advance_state(OP2, true);
            break;
        case OP2:
            putvalue(result);
            advance_state(OPCODE, true);
            break;
        default:
            break;
    }
}

static void nop()
{
    /* TODO cycle counts for undocumented nops? */
    advance_state(OPCODE, true);
}

static void ora()
{
    penaltyop = 1;
    value = getvalue();
    result = (uint16_t)a | value;

    zerocalc(result);
    signcalc(result);

    saveaccum(result);
    advance_state(OPCODE, true);
}

static void ph_()
{
    if(op_state == OP0)
    {
        /* Consume the immediate (next PC) read. */
        advance_state(OP1, true);
    }
    else
    {
        switch(opcode)
        {
            case 0x08:
                push8(status | FLAG_BREAK);
                break;
            case 0x48:
                push8(a);
                break;
            case 0x5A:
                push8(y);
                break;
            case 0xDA:
                push8(x);
                break;
            default:
                break;
        }

        advance_state(OPCODE, true);
    }
}

static void pl_(void)
{
    switch(op_state)
    {
        case OP0:
            /* Consume the immediate read state. */
            advance_state(OP1, true);
            break;
        case OP1:
            /* Read the current stack pointer before it increments. */
            (void)sys_read_mem(syscxt, BASE_STACK + sp);
            advance_state(OP2, true);
            break;
        case OP2:
            switch(opcode)
            {
                case 0x28:
                    status = pull8() | FLAG_CONSTANT; // TODO Ignore Break?
                    break;
                case 0x68:
                    a = pull8();
                    zerocalc(a);
                    signcalc(a);
                    break;
                case 0x7A:
                    y = pull8();
                    zerocalc(y);
                    signcalc(y);
                    break;
                case 0xFA:
                    x = pull8();
                    zerocalc(x);
                    signcalc(x);
                    break;
                default:
                    break;
            }

            advance_state(OPCODE, true);
            break;
        default:
            break;
    }
}

static void rol()
{
    switch(op_state)
    {
        case OP0:
            value = getvalue();
            result = (value << 1) | (status & FLAG_CARRY);

            carrycalc(result);
            zerocalc(result);
            signcalc(result);

            if(addrtable[opcode] == ACC)
            {
                putvalue(result);
                advance_state(OPCODE, true);
            }
            else
            {
                advance_state(OP1, true);
            }
            break;
        case OP1:
            (void)sys_read_mem(syscxt, ea);
            advance_state(OP2, true);
            break;
        case OP2:
            putvalue(result);
            advance_state(OPCODE, true);
            break;
        default:
            break;
    }
}

static void ror()
{
    switch(op_state)
    {
        case OP0:
            value = getvalue();
            result = (value >> 1) | ((status & FLAG_CARRY) << 7);

            if(value & 1)
                setcarry();
            else
                clearcarry();

            zerocalc(result);
            signcalc(result);

            if(addrtable[opcode] == ACC)
            {
                putvalue(result);
                advance_state(OPCODE, true);
            }
            else
            {
                advance_state(OP1, true);
            }
            break;
        case OP1:
            (void)sys_read_mem(syscxt, ea);
            advance_state(OP2, true);
            break;
        case OP2:
            putvalue(result);
            advance_state(OPCODE, true);
            break;
        default:
            break;
    }
}

static void rti()
{
    switch(op_state)
    {
        case OP0:
            advance_state(OP1, true);
            break;
        case OP1:
            (void)sys_read_mem(syscxt, BASE_STACK+sp);
            advance_state(OP2, true);
            break;
        case OP2:
            status = pull8();
            advance_state(OP3, true);
            break;
        case OP3:
            tmpval = pull8();
            advance_state(OP4, true);
            break;
        case OP4:
            tmpval |= ((uint16_t)pull8()) << 8;
            pc = tmpval;
            advance_state(OPCODE, true);
            break;
        default:
            break;
    }
}

static void rts()
{
    switch(op_state)
    {
        case OP0:
            advance_state(OP1, true);
            break;
        case OP1:
            (void)sys_read_mem(syscxt, BASE_STACK+sp);
            advance_state(OP2, true);
            break;
        case OP2:
            tmpval = pull8();
            advance_state(OP3, true);
            break;
        case OP3:
            tmpval |= (uint16_t)pull8() << 8;
            pc = tmpval;
            advance_state(OP4, true);
            break;
        case OP4:
            /* Read the last PC of the JSR, then increment to the next op. */
            (void)sys_read_mem(syscxt, pc++);
            advance_state(OPCODE, true);
            break;
        default:
            break;
    }
}

static void sbc()
{
    if(op_state == OP0)
    {
        value = getvalue() ^ 0x00FF;
        result = (uint16_t)a + value + (uint16_t)(status & FLAG_CARRY);

        carrycalc(result);
        zerocalc(result);
        overflowcalc(result, a, value);
        signcalc(result);

        if(status & FLAG_DECIMAL)
        {
            advance_state(OP1, true);
        }
        else
        {
            saveaccum(result);
            advance_state(OPCODE, true);
        }
    }
    else
    {
        clearcarry();

        a -= 0x66;
        if((a & 0x0F) > 0x09)
        {
            a += 0x06;
        }
        if((a & 0xF0) > 0x90)
        {
            a += 0x60;
            setcarry();
        }

        saveaccum(result);
        advance_state(OPCODE, true);
    }
}

static void sec()
{
    setcarry();
    advance_state(OPCODE, true);
}

static void sed()
{
    setdecimal();
    advance_state(OPCODE, true);
}

static void sei()
{
    setinterrupt();
    advance_state(OPCODE, true);
}

static void sta()
{
    cpu_addr_mode_t mode;

    if(op_state == OP0)
    {
        mode = addrtable[opcode];

        /* Store abs,X/Y instructions take an extra cycle regardless of page
         * crossing. However, if a page crossing occurred, this cycle has already
         * been handled by the address mode handler.
         */
        if((mode == ABSX || mode == ABSY) && !page_boundary)
        {
            /* The extra cycle reads the eventual write address. */
            (void)sys_read_mem(syscxt, ea);
            advance_state(OP1, true);
        }
        else if(mode == INDY && !page_boundary)
        {
            /* Another special penalty op. This team, re-read the second zp
             * address. This shoudl still be in tmpval. */
            (void)sys_read_mem(syscxt, tmpval);
            advance_state(OP1, true);
        }
        else
        {
            putvalue(a);
            advance_state(OPCODE, true);
        }
    }
    else
    {
        putvalue(a);
        advance_state(OPCODE, true);
    }
}

static void stx()
{
    cpu_addr_mode_t mode;

    if(op_state == OP0)
    {
        mode = addrtable[opcode];

        /* Store abs,X/Y instructions take an extra cycle regardless of page
         * crossing. However, if a page crossing occurred, this cycle has already
         * been handled by the address mode handler.
         */
        if((mode == ABSX || mode == ABSY) && !page_boundary)
        {
            /* The extra cycle reads the eventual write address. */
            (void)sys_read_mem(syscxt, ea);
            advance_state(OP1, true);
        }
        else
        {
            putvalue(x);
            advance_state(OPCODE, true);
        }
    }
    else
    {
        putvalue(x);
        advance_state(OPCODE, true);
    }
}

static void sty()
{
    cpu_addr_mode_t mode;

    if(op_state == OP0)
    {
        mode = addrtable[opcode];

        /* Store abs,X/Y instructions take an extra cycle regardless of page
         * crossing. However, if a page crossing occurred, this cycle has already
         * been handled by the address mode handler.
         */
        if((mode == ABSX || mode == ABSY) && !page_boundary)
        {
            /* The extra cycle reads the eventual write address. */
            (void)sys_read_mem(syscxt, ea);
            advance_state(OP1, true);
        }
        else
        {
            putvalue(y);
            advance_state(OPCODE, true);
        }
    }
    else
    {
        putvalue(y);
        advance_state(OPCODE, true);
    }
}

static void stz()
{
    cpu_addr_mode_t mode;

    if(op_state == OP0)
    {
        mode = addrtable[opcode];

        /* Store abs,X/Y instructions take an extra cycle regardless of page
         * crossing. However, if a page crossing occurred, this cycle has already
         * been handled by the address mode handler.
         */
        if((mode == ABSX || mode == ABSY) && !page_boundary)
        {
            /* The extra cycle reads the eventual write address. */
            (void)sys_read_mem(syscxt, ea);
            advance_state(OP1, true);
        }
        else
        {
            putvalue(0);
            advance_state(OPCODE, true);
        }
    }
    else
    {
        putvalue(0);
        advance_state(OPCODE, true);
    }
}

static void tax()
{
    x = a;

    zerocalc(x);
    signcalc(x);
    advance_state(OPCODE, true);
}

static void tay()
{
    y = a;

    zerocalc(y);
    signcalc(y);
    advance_state(OPCODE, true);
}

static void tsx()
{
    x = sp;

    zerocalc(x);
    signcalc(x);
    advance_state(OPCODE, true);
}

static void txa()
{
    a = x;

    zerocalc(a);
    signcalc(a);
    advance_state(OPCODE, true);
}

static void txs()
{
    sp = x;
    advance_state(OPCODE, true);
}

static void tya()
{
    a = y;

    zerocalc(a);
    signcalc(a);
    advance_state(OPCODE, true);
}

static void wai()
{
    //TODO
    advance_state(OPCODE, true);
}

static void trb()
{
    uint8_t test;

    switch(op_state)
    {
        case OP0:
            value = getvalue();

            test = value & a;
            zerocalc(test);

            result = value & ~a;
            advance_state(OP1, true);
            break;
        case OP1:
            (void)sys_read_mem(syscxt, ea);
            advance_state(OP2, true);
            break;
        case OP2:
            putvalue(result);
            advance_state(OPCODE, true);
            break;
        default:
            break;
    }
}

static void tsb()
{
    uint8_t test;

    switch(op_state)
    {
        case OP0:
            value = getvalue();

            test = value & a;
            zerocalc(test);

            result = value | a;
            advance_state(OP1, true);
            break;
        case OP1:
            (void)sys_read_mem(syscxt, ea);
            advance_state(OP2, true);
            break;
        case OP2:
            putvalue(result);
            advance_state(OPCODE, true);
            break;
        default:
            break;
    }
}

static void rmb()
{
    uint8_t bit;

    switch(op_state)
    {
        case OP0:
            /* RMBX = 0x[0-7]7, so extract the bit to reset from the opcode. */
            bit = (opcode >> 8);
            value = getvalue();
            result = value & ~(1 << bit);

            advance_state(OP1, true);
            break;
        case OP1:
            (void)sys_read_mem(syscxt, ea);
            advance_state(OP2, true);
            break;
        case OP2:
            putvalue(result);
            advance_state(OPCODE, true);
            break;
        default:
            break;
    }
}

static void smb()
{
    uint8_t bit;

    switch(op_state)
    {
        case OP0:
            /* RMBX = 0x[0-7]7, so extract the bit to reset from the opcode. */
            bit = (opcode >> 8);
            value = getvalue();
            result = value | (1 << bit);

            advance_state(OP1, true);
            break;
        case OP1:
            (void)sys_read_mem(syscxt, ea);
            advance_state(OP2, true);
            break;
        case OP2:
            putvalue(result);
            advance_state(OPCODE, true);
            break;
        default:
            break;
    }
}

static void bbr()
{
    uint8_t bit;

    switch(op_state)
    {
        case OP0:
            bit = (opcode >> 8);

            /* The value has been cached here by the address mode handler. */
            if((value && (1 << bit)) == 0)
            {
                advance_state(OP1, true);
            }
            else
            {
                advance_state(OPCODE, true);
            }
            break;
        case OP1:
            (void)sys_read_mem(syscxt, pc);

            if((pc & 0xFF00) != ((pc + reladdr) & 0xFF00))
            {
                /* Branch jumps a page, so we need to eat another cycle (PC
                 * again stays the same. */
                advance_state(OP2, true);
            }
            else
            {
                pc += reladdr;
                advance_state(OPCODE, true);
            }
            break;
        case OP2:
            (void)sys_read_mem(syscxt, pc);
            pc += reladdr;
            advance_state(OPCODE, true);
            break;
        default:
            break;
    }
}

static void bbs()
{
    uint8_t bit;

    switch(op_state)
    {
        case OP0:
            bit = (opcode >> 8);

            /* The value has been cached here by the address mode handler. */
            if((value && (1 << bit)) != 0)
            {
                advance_state(OP1, true);
            }
            else
            {
                advance_state(OPCODE, true);
            }
            break;
        case OP1:
            (void)sys_read_mem(syscxt, pc);

            if((pc & 0xFF00) != ((pc + reladdr) & 0xFF00))
            {
                /* Branch jumps a page, so we need to eat another cycle (PC
                 * again stays the same. */
                advance_state(OP2, true);
            }
            else
            {
                pc += reladdr;
                advance_state(OPCODE, true);
            }
            break;
        case OP2:
            (void)sys_read_mem(syscxt, pc);
            pc += reladdr;
            advance_state(OPCODE, true);
            break;
        default:
            break;
    }
}

static void vector(void)
{
    switch(op_state)
    {
        case VEC0:
            (void)sys_read_mem(syscxt, pc);
            advance_state(VEC1, true);
            break;
        case VEC1:
            (void)sys_read_mem(syscxt, pc);
            advance_state(VEC2, true);
            break;
        case VEC2:
            push8((pc >> 8) & 0xff);
            advance_state(VEC3, true);
            break;
        case VEC3:
            push8(pc & 0xff);
            advance_state(VEC4, true);
            break;
        case VEC4:
            if(vec_src == BRK_VEC)
            {
                push8(status | FLAG_BREAK);
            }
            else
            {
                push8(status);
            }
            status |= FLAG_INTERRUPT;
            advance_state(VEC5, true);
            break;
        case VEC5:
            switch(vec_src)
            {
                case BRK_VEC:
                case IRQ_VEC:
                    tmpval = 0xfffe;
                    break;
                case NMI_VEC:
                    tmpval = 0xfffa;
                    break;
                case RST_VEC:
                    tmpval = 0xfffc;
                    break;
                default:
                    break;
            }
            ea = sys_read_mem(syscxt, tmpval);
            advance_state(VEC6, true);
            break;
        case VEC6:
            ea |= (uint16_t)sys_read_mem(syscxt, tmpval+1) << 8;
            pc = ea;
            advance_state(OPCODE, true);
            break;
        default:
            break;
    }
}

static void (*addr_handlers[NUM_ADDR_MODES])() =
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

static void (*optable[256])() =
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
void cpu_init(sys_cxt_t system_cxt, bool reset)
{
    if(system_cxt == NULL)
        return;

    syscxt = system_cxt;
    tick_callback = NULL;

    if(reset)
        cpu_reset();
}

void cpu_tick(void)
{
    cycle_consumed = false;

    while(!cycle_consumed)
    {
        if(op_state == OPCODE)
        {
            if(sys_check_interrupt(syscxt, true))
            {
                vec_src = NMI_VEC;
                op_state = VEC0;
            }
            else if(sys_check_interrupt(syscxt, false) && !(status & FLAG_INTERRUPT))
            {
                vec_src = IRQ_VEC;
                op_state = VEC0;
            }
            else
            {
                opcode = sys_read_mem(syscxt, pc++);
                cycle_consumed = true;
                op_state = PARAM0;
            }
        }
        else if(op_state < OP0)
        {
            (*addr_handlers[addrtable[opcode]])();
        }
        else if(op_state < VEC0)
        {
            (*optable[opcode])();
        }
        else
        {
            vector();
        }
    }

    if(tick_callback)
        tick_callback(1);

    cycle_consumed = false;
}

uint8_t cpu_step()
{
    uint32_t elapsed = 0;

    /* If we are synced on the start the next opcode, go ahead and
     * tick once to read the opcode. Otherwise, skip this and just
     * loop until the current op is finished. */
    if(op_state == OPCODE)
    {
        cpu_tick();
        ++elapsed;
    }

    while(op_state != OPCODE)
    {
        cpu_tick();
        ++elapsed;
    }


    instructions++;

    return (uint8_t)elapsed;
}

void cpu_disassemble(size_t bufLen, char *buffer)
{
    cpu_disassemble_at(pc, bufLen, buffer);
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
