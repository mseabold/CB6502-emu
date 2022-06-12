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
#include "mem.h"

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
} addr_mode_t;

//6502 CPU registers
uint16_t pc;
uint8_t sp, a, x, y, status;


//helper variables
uint32_t instructions = 0; //keep track of total instructions executed
uint32_t clockticks6502 = 0, clockgoal6502 = 0;
uint16_t oldpc, ea, reladdr, value, result;
uint8_t opcode, oldstatus;

sys_cxt_t syscxt;

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


static addr_mode_t addrtable[256];
static void (*optable[256])();
uint8_t penaltyop, penaltyaddr;

//addressing mode functions, calculates effective addresses

//implied
static void imp()
{
}

//accumulator
static void acc()
{
}

//immediate
static void imm()
{
    ea = pc++;
}

//zero-page
static void zp()
{
    ea = (uint16_t)sys_read_mem(syscxt, (uint16_t)pc++);
}

//zero-page,X
static void zpx()
{
    ea = ((uint16_t)sys_read_mem(syscxt, (uint16_t)pc++) + (uint16_t)x) & 0xFF; //zero-page wraparound
}

//zero-page,Y
static void zpy()
{
    ea = ((uint16_t)sys_read_mem(syscxt, (uint16_t)pc++) + (uint16_t)y) & 0xFF; //zero-page wraparound
}

//relative for branch ops (8-bit immediate value, sign-extended)
static void rel()
{
    reladdr = (uint16_t)sys_read_mem(syscxt, pc++);
    if(reladdr & 0x80) reladdr |= 0xFF00;
}

//absolute
static void abso()
{
    ea = (uint16_t)sys_read_mem(syscxt, pc) | ((uint16_t)sys_read_mem(syscxt, pc+1) << 8);
    pc += 2;
}

//absolute,X
static void absx()
{
    uint16_t startpage;
    ea = ((uint16_t)sys_read_mem(syscxt, pc) | ((uint16_t)sys_read_mem(syscxt, pc+1) << 8));
    startpage = ea & 0xFF00;
    ea += (uint16_t)x;

    if(startpage != (ea & 0xFF00)) { //one cycle penlty for page-crossing on some opcodes
        penaltyaddr = 1;
    }

    pc += 2;
}

//absolute,Y
static void absy()
{
    uint16_t startpage;
    ea = ((uint16_t)sys_read_mem(syscxt, pc) | ((uint16_t)sys_read_mem(syscxt, pc+1) << 8));
    startpage = ea & 0xFF00;
    ea += (uint16_t)y;

    if(startpage != (ea & 0xFF00)) { //one cycle penlty for page-crossing on some opcodes
        penaltyaddr = 1;
    }

    pc += 2;
}

//indirect
static void ind()
{
    uint16_t eahelp, eahelp2;
    eahelp = (uint16_t)sys_read_mem(syscxt, pc) | (uint16_t)((uint16_t)sys_read_mem(syscxt, pc+1) << 8);
    eahelp2 = (eahelp & 0xFF00) | ((eahelp + 1) & 0x00FF); //replicate 6502 page-boundary wraparound bug
    ea = (uint16_t)sys_read_mem(syscxt, eahelp) | ((uint16_t)sys_read_mem(syscxt, eahelp2) << 8);
    pc += 2;
}

// (indirect,X)
static void indx()
{
    uint16_t eahelp;
    eahelp = (uint16_t)(((uint16_t)sys_read_mem(syscxt, pc++) + (uint16_t)x) & 0xFF); //zero-page wraparound for table pointer
    ea = (uint16_t)sys_read_mem(syscxt, eahelp & 0x00FF) | ((uint16_t)sys_read_mem(syscxt, (eahelp+1) & 0x00FF) << 8);
}

// (indirect),Y
static void indy()
{
    uint16_t eahelp, eahelp2, startpage;
    eahelp = (uint16_t)sys_read_mem(syscxt, pc++);
    eahelp2 = (eahelp & 0xFF00) | ((eahelp + 1) & 0x00FF); //zero-page wraparound
    ea = (uint16_t)sys_read_mem(syscxt, eahelp) | ((uint16_t)sys_read_mem(syscxt, eahelp2) << 8);
    startpage = ea & 0xFF00;
    ea += (uint16_t)y;

    if(startpage != (ea & 0xFF00)) { //one cycle penlty for page-crossing on some opcodes
        penaltyaddr = 1;
    }
}

// (zp)
static void indz()
{
    ea = (uint16_t)sys_read_mem(syscxt, pc++);
}

// (a,x)
static void abin()
{
    uint16_t eahelp;
    eahelp = (uint16_t) sys_read_mem(syscxt, pc) | ((uint16_t)sys_read_mem(syscxt, pc+1) << 8);
    eahelp += x;
    ea = sys_read_mem(syscxt, eahelp);
    pc += 2;
}

static void zprel()
{
    ea = sys_read_mem(syscxt, pc++);
    reladdr = (uint16_t)sys_read_mem(syscxt, pc++);

    if(reladdr & 0x80)
        reladdr |= 0xff00;
}

static uint16_t getvalue()
{
    if(addrtable[opcode] == ACC)
        return((uint16_t)a);
    else
        return((uint16_t)sys_read_mem(syscxt, ea));
}

static uint16_t getvalue16()
{
    return((uint16_t)sys_read_mem(syscxt, ea) | ((uint16_t)sys_read_mem(syscxt, ea+1) << 8));
}

static void putvalue(uint16_t saveval)
{
    if(addrtable[opcode] == ACC)
        a = (uint8_t)(saveval & 0x00FF);
    else
        sys_write_mem(syscxt, ea, (saveval & 0x00FF));
}


//instruction handler functions
static void adc()
{
    penaltyop = 1;
    value = getvalue();
    result = (uint16_t)a + value + (uint16_t)(status & FLAG_CARRY);

    carrycalc(result);
    zerocalc(result);
    overflowcalc(result, a, value);
    signcalc(result);

    if(status & FLAG_DECIMAL)
    {
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

        clockticks6502++;
    }

    saveaccum(result);
}

static void and()
{
    penaltyop = 1;
    value = getvalue();
    result = (uint16_t)a & value;

    zerocalc(result);
    signcalc(result);

    saveaccum(result);
}

static void asl()
{
    value = getvalue();
    result = value << 1;

    carrycalc(result);
    zerocalc(result);
    signcalc(result);

    putvalue(result);
}

static void bcc()
{
    if((status & FLAG_CARRY) == 0)
    {
        oldpc = pc;
        pc += reladdr;
        if((oldpc & 0xFF00) != (pc & 0xFF00))
            clockticks6502 += 2; //check if jump crossed a page boundary
        else
            clockticks6502++;
    }
}

static void bcs()
{
    if((status & FLAG_CARRY) == FLAG_CARRY)
    {
        oldpc = pc;
        pc += reladdr;
        if((oldpc & 0xFF00) != (pc & 0xFF00))
            clockticks6502 += 2; //check if jump crossed a page boundary
        else
            clockticks6502++;
    }
}

static void beq()
{
    if((status & FLAG_ZERO) == FLAG_ZERO)
    {
        oldpc = pc;
        pc += reladdr;
        if((oldpc & 0xFF00) != (pc & 0xFF00))
            clockticks6502 += 2; //check if jump crossed a page boundary
        else
            clockticks6502++;
    }
}

static void bit()
{
    value = getvalue();
    result = (uint16_t)a & value;

    zerocalc(result);
    status = (status & 0x3F) | (uint8_t)(value & 0xC0);
}

static void bmi()
{
    if((status & FLAG_SIGN) == FLAG_SIGN)
    {
        oldpc = pc;
        pc += reladdr;
        if((oldpc & 0xFF00) != (pc & 0xFF00))
            clockticks6502 += 2; //check if jump crossed a page boundary
        else
            clockticks6502++;
    }
}

static void bne()
{
    if((status & FLAG_ZERO) == 0)
    {
        oldpc = pc;
        pc += reladdr;
        if((oldpc & 0xFF00) != (pc & 0xFF00))
            clockticks6502 += 2; //check if jump crossed a page boundary
        else
            clockticks6502++;
    }
}

static void bpl()
{
    if((status & FLAG_SIGN) == 0)
    {
        oldpc = pc;
        pc += reladdr;
        if((oldpc & 0xFF00) != (pc & 0xFF00))
            clockticks6502 += 2; //check if jump crossed a page boundary
        else
            clockticks6502++;
    }
}

#ifdef SUPPORT_65C02
static void bra()
{
    oldpc = pc;
    pc += reladdr;
    if((oldpc & 0xFF00) != (pc & 0xFF00))
        clockticks6502 += 2; //check if jump crossed a page boundary
    else
        clockticks6502++;
}
#endif

static void brk()
{
    pc++;
    push16(pc); //push next instruction address onto stack
    push8(status | FLAG_BREAK); //push CPU status to stack
    setinterrupt(); //set interrupt flag
    pc = (uint16_t)sys_read_mem(syscxt, 0xFFFE) | ((uint16_t)sys_read_mem(syscxt, 0xFFFF) << 8);
}

static void bvc()
{
    if((status & FLAG_OVERFLOW) == 0)
    {
        oldpc = pc;
        pc += reladdr;
        if((oldpc & 0xFF00) != (pc & 0xFF00))
            clockticks6502 += 2; //check if jump crossed a page boundary
        else
            clockticks6502++;
    }
}

static void bvs()
{
    if((status & FLAG_OVERFLOW) == FLAG_OVERFLOW)
    {
        oldpc = pc;
        pc += reladdr;
        if((oldpc & 0xFF00) != (pc & 0xFF00))
            clockticks6502 += 2; //check if jump crossed a page boundary
        else
            clockticks6502++;
    }
}

static void clc()
{
    clearcarry();
}

static void cld()
{
    cleardecimal();
}

static void cli()
{
    clearinterrupt();
}

static void clv()
{
    clearoverflow();
}

static void cmp()
{
    penaltyop = 1;
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
}

static void dec()
{
    value = getvalue();
    result = value - 1;

    zerocalc(result);
    signcalc(result);

    putvalue(result);
}

static void dex()
{
    x--;

    zerocalc(x);
    signcalc(x);
}

static void dey()
{
    y--;

    zerocalc(y);
    signcalc(y);
}

static void eor()
{
    penaltyop = 1;
    value = getvalue();
    result = (uint16_t)a ^ value;

    zerocalc(result);
    signcalc(result);

    saveaccum(result);
}

static void inc()
{
    value = getvalue();
    result = value + 1;

    zerocalc(result);
    signcalc(result);

    putvalue(result);
}

static void inx()
{
    x++;

    zerocalc(x);
    signcalc(x);
}

static void iny()
{
    y++;

    zerocalc(y);
    signcalc(y);
}

static void jmp()
{
    pc = ea;
}

static void jsr()
{
    push16(pc - 1);
    pc = ea;
}

static void lda()
{
    penaltyop = 1;
    value = getvalue();
    a = (uint8_t)(value & 0x00FF);

    zerocalc(a);
    signcalc(a);
}

static void ldx()
{
    penaltyop = 1;
    value = getvalue();
    x = (uint8_t)(value & 0x00FF);

    zerocalc(x);
    signcalc(x);
}

static void ldy()
{
    penaltyop = 1;
    value = getvalue();
    y = (uint8_t)(value & 0x00FF);

    zerocalc(y);
    signcalc(y);
}

static void lsr()
{
    value = getvalue();
    result = value >> 1;

    if(value & 1)
        setcarry();
    else
        clearcarry();

    zerocalc(result);
    signcalc(result);

    putvalue(result);
}

static void nop()
{
    switch (opcode)
    {
        /* TODO is this still the case in 65C02? */
        case 0x1C:
        case 0x3C:
        case 0x5C:
        case 0x7C:
        case 0xDC:
        case 0xFC:
            penaltyop = 1;
            break;
    }
}

static void ora()
{
    penaltyop = 1;
    value = getvalue();
    result = (uint16_t)a | value;

    zerocalc(result);
    signcalc(result);

    saveaccum(result);
}

static void pha()
{
    push8(a);
}

static void php()
{
    push8(status | FLAG_BREAK);
}

static void phx()
{
    push8(x);
}

static void phy()
{
    push8(y);
}

static void pla()
{
    a = pull8();

    zerocalc(a);
    signcalc(a);
}

static void plp()
{
    status = pull8() | FLAG_CONSTANT;
}

static void plx()
{
    x = pull8();

    zerocalc(x);
    signcalc(x);
}

static void ply()
{
    y = pull8();

    zerocalc(y);
    signcalc(y);
}

static void rol()
{
    value = getvalue();
    result = (value << 1) | (status & FLAG_CARRY);

    carrycalc(result);
    zerocalc(result);
    signcalc(result);

    putvalue(result);
}

static void ror()
{
    value = getvalue();
    result = (value >> 1) | ((status & FLAG_CARRY) << 7);

    if(value & 1)
        setcarry();
    else
        clearcarry();

    zerocalc(result);
    signcalc(result);

    putvalue(result);
}

static void rti()
{
    status = pull8();
    value = pull16();
    pc = value;
}

static void rts()
{
    value = pull16();
    pc = value + 1;
}

static void sbc()
{
    penaltyop = 1;
    value = getvalue() ^ 0x00FF;
    result = (uint16_t)a + value + (uint16_t)(status & FLAG_CARRY);

    carrycalc(result);
    zerocalc(result);
    overflowcalc(result, a, value);
    signcalc(result);

    if(status & FLAG_DECIMAL)
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

        clockticks6502++;
    }

    saveaccum(result);
}

static void sec()
{
    setcarry();
}

static void sed()
{
    setdecimal();
}

static void sei()
{
    setinterrupt();
}

static void sta()
{
    putvalue(a);
}

static void stx()
{
    putvalue(x);
}

static void sty()
{
    putvalue(y);
}

static void stz()
{
    putvalue(0);
}

static void tax()
{
    x = a;

    zerocalc(x);
    signcalc(x);
}

static void tay()
{
    y = a;

    zerocalc(y);
    signcalc(y);
}

static void tsx()
{
    x = sp;

    zerocalc(x);
    signcalc(x);
}

static void txa()
{
    a = x;

    zerocalc(a);
    signcalc(a);
}

static void txs()
{
    sp = x;
}

static void tya()
{
    a = y;

    zerocalc(a);
    signcalc(a);
}

static void wai()
{
    //XXX
    while(1);
}

static void trb()
{
    uint8_t test;
    uint8_t result;
    uint8_t value;

    value = getvalue();

    test = value & a;
    zerocalc(test);
    result = value & ~a;
    saveaccum(result);
}

static void tsb()
{
    uint8_t test;
    uint8_t result;
    uint8_t value;

    value = getvalue();

    test = value & a;
    zerocalc(test);
    result = value | a;
    saveaccum(result);
}

static void rmb()
{
    uint8_t bit;
    uint8_t value;
    uint8_t result;

    /* RMBX = 0x[0-7]7, so extract the bit to reset from the opcode. */
    bit = (opcode >> 8);
    value = getvalue();
    result = value & ~(1 << bit);

    saveaccum(result);
}

static void smb()
{
    uint8_t bit;
    uint8_t value;
    uint8_t result;

    /* SMBX = 0x[8-F]7, so extract the bit to reset from the opcode. */
    bit = (opcode >> 8) - 8;
    value = getvalue();
    result = value | (1 << bit);

    saveaccum(result);
}

static void bbr()
{
    uint8_t bit;
    uint8_t value;

    bit = (opcode >> 8);
    value = getvalue();

    if((value & bit) == 0)
    {
        oldpc = pc;
        pc += reladdr;
        if((oldpc & 0xFF00) != (pc & 0xFF00))
            clockticks6502 += 2; //check if jump crossed a page boundary
        else
            clockticks6502++;
    }
}

static void bbs()
{
    uint8_t bit;
    uint8_t value;

    bit = (opcode >> 8);
    value = getvalue();

    if((value & bit) != 0)
    {
        oldpc = pc;
        pc += reladdr;
        if((oldpc & 0xFF00) != (pc & 0xFF00))
            clockticks6502 += 2; //check if jump crossed a page boundary
        else
            clockticks6502++;
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
    zprel
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

static addr_mode_t addrtable[256] =
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
/* 0 */      brk,  ora,  nop,  nop,  tsb,  ora,  asl,  rmb,  php,  ora,  asl,  nop,  tsb,  ora,  asl,  bbr, /* 0 */
/* 1 */      bpl,  ora,  ora,  nop,  trb,  ora,  asl,  rmb,  clc,  ora,  inc,  nop,  trb,  ora,  asl,  bbr, /* 1 */
/* 2 */      jsr,  and,  nop,  nop,  bit,  and,  rol,  rmb,  plp,  and,  rol,  nop,  bit,  and,  rol,  bbr, /* 2 */
/* 3 */      bmi,  and,  and,  nop,  bit,  and,  rol,  rmb,  sec,  and,  dec,  nop,  bit,  and,  rol,  bbr, /* 3 */
/* 4 */      rti,  eor,  nop,  nop,  nop,  eor,  lsr,  rmb,  pha,  eor,  lsr,  nop,  jmp,  eor,  lsr,  bbr, /* 4 */
/* 5 */      bvc,  eor,  eor,  nop,  nop,  eor,  lsr,  rmb,  cli,  eor,  phy,  nop,  nop,  eor,  lsr,  bbr, /* 5 */
/* 6 */      rts,  adc,  nop,  nop,  stz,  adc,  ror,  rmb,  pla,  adc,  ror,  nop,  jmp,  adc,  ror,  bbr, /* 6 */
/* 7 */      bvs,  adc,  adc,  nop,  stz,  adc,  ror,  rmb,  sei,  adc,  ply,  nop,  jmp,  adc,  ror,  bbr, /* 7 */
/* 8 */      bra,  sta,  nop,  nop,  sty,  sta,  stx,  smb,  dey,  bit,  txa,  nop,  sty,  sta,  stx,  bbs, /* 8 */
/* 9 */      bcc,  sta,  sta,  nop,  sty,  sta,  stx,  smb,  tya,  sta,  txs,  nop,  stz,  sta,  stz,  bbs, /* 9 */
/* A */      ldy,  lda,  ldx,  nop,  ldy,  lda,  ldx,  smb,  tay,  lda,  tax,  nop,  ldy,  lda,  ldx,  bbs, /* A */
/* B */      bcs,  lda,  lda,  nop,  ldy,  lda,  ldx,  smb,  clv,  lda,  tsx,  nop,  ldy,  lda,  ldx,  bbs, /* B */
/* C */      cpy,  cmp,  nop,  nop,  cpy,  cmp,  dec,  smb,  iny,  cmp,  dex,  wai,  cpy,  cmp,  dec,  bbs, /* C */
/* D */      bne,  cmp,  cmp,  nop,  nop,  cmp,  dec,  smb,  cld,  cmp,  phx,  nop,  nop,  cmp,  dec,  bbs, /* D */
/* E */      cpx,  sbc,  nop,  nop,  cpx,  sbc,  inc,  smb,  inx,  sbc,  nop,  nop,  cpx,  sbc,  inc,  bbs, /* E */
/* F */      beq,  sbc,  sbc,  nop,  nop,  sbc,  inc,  smb,  sed,  sbc,  plx,  nop,  nop,  sbc,  inc,  bbs  /* F */
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

static const char *mnemonics[256] =
{
/*         | 0    |  1    |  2    |  3    |  4    |  5    |  6    |  7    |  8    |  9    |  A    |  B    |  C    |  D    |  E    |  F  |     */
/* 0 */      "BRK",  "ORA",  "NOP",  "NOP",  "TSB",  "ORA",  "ASL", "RMB0",  "PHP",  "ORA",  "ASL",  "NOP",  "TSB",  "ORA",  "ASL",  "SLO", /* 0 */
/* 1 */      "BPL",  "ORA",  "ORA",  "NOP",  "TRB",  "ORA",  "ASL", "RMB1",  "CLC",  "ORA",  "NOP",  "NOP",  "TRB",  "ORA",  "ASL",  "SLO", /* 1 */
/* 2 */      "JSR",  "AND",  "NOP",  "NOP",  "BIT",  "AND",  "ROL", "RMB2",  "PLP",  "AND",  "ROL",  "NOP",  "BIT",  "AND",  "ROL",  "RLA", /* 2 */
/* 3 */      "BMI",  "AND",  "AND",  "NOP",  "BIT",  "AND",  "ROL", "RMB3",  "SEC",  "AND",  "NOP",  "NOP",  "BIT",  "AND",  "ROL",  "RLA", /* 3 */
/* 4 */      "RTI",  "EOR",  "NOP",  "NOP",  "NOP",  "EOR",  "LSR", "RMB4",  "PHA",  "EOR",  "LSR",  "NOP",  "JMP",  "EOR",  "LSR",  "SRE", /* 4 */
/* 5 */      "BVC",  "EOR",  "EOR",  "NOP",  "NOP",  "EOR",  "LSR", "RMB5",  "CLI",  "EOR",  "PHY",  "NOP",  "NOP",  "EOR",  "LSR",  "SRE", /* 5 */
/* 6 */      "RTS",  "ADC",  "NOP",  "NOP",  "STZ",  "ADC",  "ROR", "RMB6",  "PLA",  "ADC",  "ROR",  "NOP",  "JMP",  "ADC",  "ROR",  "RRA", /* 6 */
/* 7 */      "BVS",  "ADC",  "ADC",  "NOP",  "STZ",  "ADC",  "ROR", "RMB7",  "SEI",  "ADC",  "PLY",  "NOP",  "JMP",  "ADC",  "ROR",  "RRA", /* 7 */
/* 8 */      "BRA",  "STA",  "NOP",  "NOP",  "STY",  "STA",  "STX", "SMB0",  "DEY",  "BIT",  "TXA",  "NOP",  "STY",  "STA",  "STX",  "SAX", /* 8 */
/* 9 */      "BCC",  "STA",  "STA",  "NOP",  "STY",  "STA",  "STX", "SMB1",  "TYA",  "STA",  "TXS",  "NOP",  "STZ",  "STA",  "STZ",  "NOP", /* 9 */
/* A */      "LDY",  "LDA",  "LDX",  "NOP",  "LDY",  "LDA",  "LDX", "SMB2",  "TAY",  "LDA",  "TAX",  "NOP",  "LDY",  "LDA",  "LDX",  "LAX", /* A */
/* B */      "BCS",  "LDA",  "LDA",  "NOP",  "LDY",  "LDA",  "LDX", "SMB3",  "CLV",  "LDA",  "TSX",  "NOP",  "LDY",  "LDA",  "LDX",  "LAX", /* B */
/* C */      "CPY",  "CMP",  "NOP",  "NOP",  "CPY",  "CMP",  "DEC", "SMB4",  "INY",  "CMP",  "DEX",  "WAI",  "CPY",  "CMP",  "DEC",  "DCP", /* C */
/* D */      "BNE",  "CMP",  "CMP",  "NOP",  "NOP",  "CMP",  "DEC", "SMB5",  "CLD",  "CMP",  "PHX",  "STP",  "NOP",  "CMP",  "DEC",  "DCP", /* D */
/* E */      "CPX",  "SBC",  "NOP",  "NOP",  "CPX",  "SBC",  "INC", "SMB6",  "INX",  "SBC",  "NOP",  "NOP",  "CPX",  "SBC",  "INC",  "ISB", /* E */
/* F */      "BEQ",  "SBC",  "SBC",  "NOP",  "NOP",  "SBC",  "INC", "SMB7",  "SED",  "SBC",  "PLX",  "NOP",  "NOP",  "SBC",  "INC",  "ISB"  /* F */
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

    if(reset)
        cpu_reset();
}

void nmi6502()
{
    push16(pc);
    push8(status);
    status |= FLAG_INTERRUPT;
    pc = (uint16_t)sys_read_mem(syscxt, 0xFFFA) | ((uint16_t)sys_read_mem(syscxt, 0xFFFB) << 8);
}

void irq6502()
{
    push16(pc);
    push8(status);
    status |= FLAG_INTERRUPT;
    pc = (uint16_t)sys_read_mem(syscxt, 0xFFFE) | ((uint16_t)sys_read_mem(syscxt, 0xFFFF) << 8);
}

uint8_t cpu_step()
{
    opcode = sys_read_mem(syscxt, pc++);
    status |= FLAG_CONSTANT;
    uint32_t startticks = clockticks6502;

    penaltyop = 0;
    penaltyaddr = 0;

    (*addr_handlers[addrtable[opcode]])();
    (*optable[opcode])();
    clockticks6502 += ticktable[opcode];
    if(penaltyop && penaltyaddr) clockticks6502++;
    clockgoal6502 = clockticks6502;

    instructions++;

    return (uint8_t)(clockticks6502 - startticks);
}

void cpu_disassemble(size_t bufLen, char *buffer)
{
    const char *mn;
    addr_mode_t addr_mode;
    uint8_t opcode = sys_read_mem(syscxt, pc);
    size_t len;
    mn = mnemonics[opcode];
    addr_mode = addrtable[opcode];
    snprintf(buffer, bufLen, "0x%04x: %s", pc, mn);

    len = strlen(buffer);

    buffer += len;
    bufLen -= len;

    if(bufLen == 0)
        return;

    switch(addr_mode)
    {
        case IMM:
            snprintf(buffer, bufLen, " #$%02x", sys_read_mem(syscxt, pc+1));
            break;
        case ZP:
            snprintf(buffer, bufLen, " $00%02x", sys_read_mem(syscxt, pc+1));
            break;
        case ZPX:
            snprintf(buffer, bufLen, " $00%02x,X", sys_read_mem(syscxt, pc+1));
            break;
        case ZPY:
            snprintf(buffer, bufLen, " $00%02x,Y", sys_read_mem(syscxt, pc+1));
            break;
        case REL:
            snprintf(buffer, bufLen, " $%02x", sys_read_mem(syscxt, pc+1));
            break;
        case ABSO:
            snprintf(buffer, bufLen, " $%02x%02x", sys_read_mem(syscxt, pc+2), sys_read_mem(syscxt, pc+1));
            break;
        case ABSX:
            snprintf(buffer, bufLen, " $%02x%02x,X", sys_read_mem(syscxt, pc+2), sys_read_mem(syscxt, pc+1));
            break;
        case ABSY:
            snprintf(buffer, bufLen, " $%02x%02x,Y", sys_read_mem(syscxt, pc+2), sys_read_mem(syscxt, pc+1));
            break;
        case IND:
            snprintf(buffer, bufLen, " ($%02x%02x)", sys_read_mem(syscxt, pc+2), sys_read_mem(syscxt, pc+1));
            break;
        case INDX:
            snprintf(buffer, bufLen, " ($00%02x,X)", sys_read_mem(syscxt, pc+1));
            break;
        case INDY:
            snprintf(buffer, bufLen, " ($00%02x,Y)", sys_read_mem(syscxt, pc+1));
            break;
        case INDZ:
            snprintf(buffer, bufLen, " ($00%02x)", sys_read_mem(syscxt, pc+1));
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
    uint8_t opcode = sys_read_mem(syscxt, pc);
    return addr_lengths[addrtable[opcode]];
}

bool cpu_is_subroutine(void)
{
    uint8_t opcode = sys_read_mem(syscxt, pc);

    return opcode == 0x20;
}
