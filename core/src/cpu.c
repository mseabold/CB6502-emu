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

//6502 defines
#define UNDOCUMENTED //when this is defined, undocumented opcodes are handled.
                     //otherwise, they're simply treated as NOPs.

#if 0
#define NES_CPU      //when this is defined, the binary-coded decimal (BCD)
                     //status flag is not honored by ADC and SBC. the 2A03
                     //CPU in the Nintendo Entertainment System does not
                     //support BCD operation.
#endif

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
#define zerocalc(n) {\
    if ((n) & 0x00FF) clearzero();\
        else setzero();\
}

#define signcalc(n) {\
    if ((n) & 0x0080) setsign();\
        else clearsign();\
}

#define carrycalc(n) {\
    if ((n) & 0xFF00) setcarry();\
        else clearcarry();\
}

#define overflowcalc(n, m, o) { /* n = result, m = accumulator, o = memory */ \
    if (((n) ^ (uint16_t)(m)) & ((n) ^ (o)) & 0x0080) setoverflow();\
        else clearoverflow();\
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
} addr_mode_t;

static uint8_t dummy_read(uint16_t address)
{
    return 0xff;
}

static void dummy_write(uint16_t address, uint8_t value)
{
}

static const mem_space_t dummy_space =
{
    dummy_write,
    dummy_read
};

//6502 CPU registers
uint16_t pc;
uint8_t sp, a, x, y, status;


//helper variables
uint32_t instructions = 0; //keep track of total instructions executed
uint32_t clockticks6502 = 0, clockgoal6502 = 0;
uint16_t oldpc, ea, reladdr, value, result;
uint8_t opcode, oldstatus;

// Callback functions registers via init
// Initially set them to empty implementations (as opposed to adding a NULL check
// to every single call)
static mem_space_t *mem_space = (mem_space_t *)&dummy_space;

//a few general functions used by various other functions
void push16(uint16_t pushval) {
    mem_space->write(BASE_STACK + sp, (pushval >> 8) & 0xFF);
    mem_space->write(BASE_STACK + ((sp - 1) & 0xFF), pushval & 0xFF);
    sp -= 2;
}

void push8(uint8_t pushval) {
    mem_space->write(BASE_STACK + sp--, pushval);
}

uint16_t pull16() {
    uint16_t temp16;
    temp16 = mem_space->read(BASE_STACK + ((sp + 1) & 0xFF)) | ((uint16_t)mem_space->read(BASE_STACK + ((sp + 2) & 0xFF)) << 8);
    sp += 2;
    return(temp16);
}

uint8_t pull8() {
    return (mem_space->read(BASE_STACK + ++sp));
}

void reset6502() {
    pc = (uint16_t)mem_space->read(0xFFFC) | ((uint16_t)mem_space->read(0xFFFD) << 8);
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
static void imp() { //implied
}

static void acc() { //accumulator
}

static void imm() { //immediate
    ea = pc++;
}

static void zp() { //zero-page
    ea = (uint16_t)mem_space->read((uint16_t)pc++);
}

static void zpx() { //zero-page,X
    ea = ((uint16_t)mem_space->read((uint16_t)pc++) + (uint16_t)x) & 0xFF; //zero-page wraparound
}

static void zpy() { //zero-page,Y
    ea = ((uint16_t)mem_space->read((uint16_t)pc++) + (uint16_t)y) & 0xFF; //zero-page wraparound
}

static void rel() { //relative for branch ops (8-bit immediate value, sign-extended)
    reladdr = (uint16_t)mem_space->read(pc++);
    if (reladdr & 0x80) reladdr |= 0xFF00;
}

static void abso() { //absolute
    ea = (uint16_t)mem_space->read(pc) | ((uint16_t)mem_space->read(pc+1) << 8);
    pc += 2;
}

static void absx() { //absolute,X
    uint16_t startpage;
    ea = ((uint16_t)mem_space->read(pc) | ((uint16_t)mem_space->read(pc+1) << 8));
    startpage = ea & 0xFF00;
    ea += (uint16_t)x;

    if (startpage != (ea & 0xFF00)) { //one cycle penlty for page-crossing on some opcodes
        penaltyaddr = 1;
    }

    pc += 2;
}

static void absy() { //absolute,Y
    uint16_t startpage;
    ea = ((uint16_t)mem_space->read(pc) | ((uint16_t)mem_space->read(pc+1) << 8));
    startpage = ea & 0xFF00;
    ea += (uint16_t)y;

    if (startpage != (ea & 0xFF00)) { //one cycle penlty for page-crossing on some opcodes
        penaltyaddr = 1;
    }

    pc += 2;
}

static void ind() { //indirect
    uint16_t eahelp, eahelp2;
    eahelp = (uint16_t)mem_space->read(pc) | (uint16_t)((uint16_t)mem_space->read(pc+1) << 8);
    eahelp2 = (eahelp & 0xFF00) | ((eahelp + 1) & 0x00FF); //replicate 6502 page-boundary wraparound bug
    ea = (uint16_t)mem_space->read(eahelp) | ((uint16_t)mem_space->read(eahelp2) << 8);
    pc += 2;
}

static void indx() { // (indirect,X)
    uint16_t eahelp;
    eahelp = (uint16_t)(((uint16_t)mem_space->read(pc++) + (uint16_t)x) & 0xFF); //zero-page wraparound for table pointer
    ea = (uint16_t)mem_space->read(eahelp & 0x00FF) | ((uint16_t)mem_space->read((eahelp+1) & 0x00FF) << 8);
}

static void indy() { // (indirect),Y
    uint16_t eahelp, eahelp2, startpage;
    eahelp = (uint16_t)mem_space->read(pc++);
    eahelp2 = (eahelp & 0xFF00) | ((eahelp + 1) & 0x00FF); //zero-page wraparound
    ea = (uint16_t)mem_space->read(eahelp) | ((uint16_t)mem_space->read(eahelp2) << 8);
    startpage = ea & 0xFF00;
    ea += (uint16_t)y;

    if (startpage != (ea & 0xFF00)) { //one cycle penlty for page-crossing on some opcodes
        penaltyaddr = 1;
    }
}

static void indz() { // (zp)
    ea = (uint16_t)mem_space->read(pc++);
}

static void abin() { // (a,x)
    uint16_t eahelp;
    eahelp = (uint16_t) mem_space->read(pc) | ((uint16_t)mem_space->read(pc+1) << 8);
    eahelp += x;
    ea = mem_space->read(eahelp);
    pc += 2;
}

static void zprel() {
    ea = mem_space->read(pc++);
    reladdr = (uint16_t)mem_space->read(pc++);

    if(reladdr & 0x80)
        reladdr |= 0xff00;
}

static uint16_t getvalue() {
    if (addrtable[opcode] == ACC) return((uint16_t)a);
        else return((uint16_t)mem_space->read(ea));
}

static uint16_t getvalue16() {
    return((uint16_t)mem_space->read(ea) | ((uint16_t)mem_space->read(ea+1) << 8));
}

static void putvalue(uint16_t saveval) {
    if (addrtable[opcode] == ACC) a = (uint8_t)(saveval & 0x00FF);
        else mem_space->write(ea, (saveval & 0x00FF));
}


//instruction handler functions
static void adc() {
    penaltyop = 1;
    value = getvalue();
    result = (uint16_t)a + value + (uint16_t)(status & FLAG_CARRY);
   
    carrycalc(result);
    zerocalc(result);
    overflowcalc(result, a, value);
    signcalc(result);
    
    #ifndef NES_CPU
    if (status & FLAG_DECIMAL) {
        clearcarry();
        
        if ((a & 0x0F) > 0x09) {
            a += 0x06;
        }
        if ((a & 0xF0) > 0x90) {
            a += 0x60;
            setcarry();
        }
        
        clockticks6502++;
    }
    #endif
   
    saveaccum(result);
}

static void and() {
    penaltyop = 1;
    value = getvalue();
    result = (uint16_t)a & value;
   
    zerocalc(result);
    signcalc(result);
   
    saveaccum(result);
}

static void asl() {
    value = getvalue();
    result = value << 1;

    carrycalc(result);
    zerocalc(result);
    signcalc(result);
   
    putvalue(result);
}

static void bcc() {
    if ((status & FLAG_CARRY) == 0) {
        oldpc = pc;
        pc += reladdr;
        if ((oldpc & 0xFF00) != (pc & 0xFF00)) clockticks6502 += 2; //check if jump crossed a page boundary
            else clockticks6502++;
    }
}

static void bcs() {
    if ((status & FLAG_CARRY) == FLAG_CARRY) {
        oldpc = pc;
        pc += reladdr;
        if ((oldpc & 0xFF00) != (pc & 0xFF00)) clockticks6502 += 2; //check if jump crossed a page boundary
            else clockticks6502++;
    }
}

static void beq() {
    if ((status & FLAG_ZERO) == FLAG_ZERO) {
        oldpc = pc;
        pc += reladdr;
        if ((oldpc & 0xFF00) != (pc & 0xFF00)) clockticks6502 += 2; //check if jump crossed a page boundary
            else clockticks6502++;
    }
}

static void bit() {
    value = getvalue();
    result = (uint16_t)a & value;
   
    zerocalc(result);
    status = (status & 0x3F) | (uint8_t)(value & 0xC0);
}

static void bmi() {
    if ((status & FLAG_SIGN) == FLAG_SIGN) {
        oldpc = pc;
        pc += reladdr;
        if ((oldpc & 0xFF00) != (pc & 0xFF00)) clockticks6502 += 2; //check if jump crossed a page boundary
            else clockticks6502++;
    }
}

static void bne() {
    if ((status & FLAG_ZERO) == 0) {
        oldpc = pc;
        pc += reladdr;
        if ((oldpc & 0xFF00) != (pc & 0xFF00)) clockticks6502 += 2; //check if jump crossed a page boundary
            else clockticks6502++;
    }
}

static void bpl() {
    if ((status & FLAG_SIGN) == 0) {
        oldpc = pc;
        pc += reladdr;
        if ((oldpc & 0xFF00) != (pc & 0xFF00)) clockticks6502 += 2; //check if jump crossed a page boundary
            else clockticks6502++;
    }
}

#ifdef SUPPORT_65C02
static void bra() {
    oldpc = pc;
    pc += reladdr;
    if ((oldpc & 0xFF00) != (pc & 0xFF00)) clockticks6502 += 2; //check if jump crossed a page boundary
        else clockticks6502++;
}
#endif

static void brk() {
    pc++;
    push16(pc); //push next instruction address onto stack
    push8(status | FLAG_BREAK); //push CPU status to stack
    setinterrupt(); //set interrupt flag
    pc = (uint16_t)mem_space->read(0xFFFE) | ((uint16_t)mem_space->read(0xFFFF) << 8);
}

static void bvc() {
    if ((status & FLAG_OVERFLOW) == 0) {
        oldpc = pc;
        pc += reladdr;
        if ((oldpc & 0xFF00) != (pc & 0xFF00)) clockticks6502 += 2; //check if jump crossed a page boundary
            else clockticks6502++;
    }
}

static void bvs() {
    if ((status & FLAG_OVERFLOW) == FLAG_OVERFLOW) {
        oldpc = pc;
        pc += reladdr;
        if ((oldpc & 0xFF00) != (pc & 0xFF00)) clockticks6502 += 2; //check if jump crossed a page boundary
            else clockticks6502++;
    }
}

static void clc() {
    clearcarry();
}

static void cld() {
    cleardecimal();
}

static void cli() {
    clearinterrupt();
}

static void clv() {
    clearoverflow();
}

static void cmp() {
    penaltyop = 1;
    value = getvalue();
    result = (uint16_t)a - value;
   
    if (a >= (uint8_t)(value & 0x00FF)) setcarry();
        else clearcarry();
    if (a == (uint8_t)(value & 0x00FF)) setzero();
        else clearzero();
    signcalc(result);
}

static void cpx() {
    value = getvalue();
    result = (uint16_t)x - value;
   
    if (x >= (uint8_t)(value & 0x00FF)) setcarry();
        else clearcarry();
    if (x == (uint8_t)(value & 0x00FF)) setzero();
        else clearzero();
    signcalc(result);
}

static void cpy() {
    value = getvalue();
    result = (uint16_t)y - value;
   
    if (y >= (uint8_t)(value & 0x00FF)) setcarry();
        else clearcarry();
    if (y == (uint8_t)(value & 0x00FF)) setzero();
        else clearzero();
    signcalc(result);
}

static void dec() {
    value = getvalue();
    result = value - 1;
   
    zerocalc(result);
    signcalc(result);
   
    putvalue(result);
}

static void dex() {
    x--;
   
    zerocalc(x);
    signcalc(x);
}

static void dey() {
    y--;
   
    zerocalc(y);
    signcalc(y);
}

static void eor() {
    penaltyop = 1;
    value = getvalue();
    result = (uint16_t)a ^ value;
   
    zerocalc(result);
    signcalc(result);
   
    saveaccum(result);
}

static void inc() {
    value = getvalue();
    result = value + 1;
   
    zerocalc(result);
    signcalc(result);
   
    putvalue(result);
}

static void inx() {
    x++;
   
    zerocalc(x);
    signcalc(x);
}

static void iny() {
    y++;
   
    zerocalc(y);
    signcalc(y);
}

static void jmp() {
    pc = ea;
}

static void jsr() {
    push16(pc - 1);
    pc = ea;
}

static void lda() {
    penaltyop = 1;
    value = getvalue();
    a = (uint8_t)(value & 0x00FF);
   
    zerocalc(a);
    signcalc(a);
}

static void ldx() {
    penaltyop = 1;
    value = getvalue();
    x = (uint8_t)(value & 0x00FF);
   
    zerocalc(x);
    signcalc(x);
}

static void ldy() {
    penaltyop = 1;
    value = getvalue();
    y = (uint8_t)(value & 0x00FF);
   
    zerocalc(y);
    signcalc(y);
}

static void lsr() {
    value = getvalue();
    result = value >> 1;
   
    if (value & 1) setcarry();
        else clearcarry();
    zerocalc(result);
    signcalc(result);
   
    putvalue(result);
}

static void nop() {
    switch (opcode) {
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

static void ora() {
    penaltyop = 1;
    value = getvalue();
    result = (uint16_t)a | value;
   
    zerocalc(result);
    signcalc(result);
   
    saveaccum(result);
}

static void pha() {
    push8(a);
}

static void php() {
    push8(status | FLAG_BREAK);
}

static void phx() {
    push8(x);
}

static void phy() {
    push8(y);
}

static void pla() {
    a = pull8();
   
    zerocalc(a);
    signcalc(a);
}

static void plp() {
    status = pull8() | FLAG_CONSTANT;
}

static void plx() {
    x = pull8();

    zerocalc(x);
    signcalc(x);
}

static void ply() {
    y = pull8();

    zerocalc(y);
    signcalc(y);
}

static void rol() {
    value = getvalue();
    result = (value << 1) | (status & FLAG_CARRY);
   
    carrycalc(result);
    zerocalc(result);
    signcalc(result);
   
    putvalue(result);
}

static void ror() {
    value = getvalue();
    result = (value >> 1) | ((status & FLAG_CARRY) << 7);
   
    if (value & 1) setcarry();
        else clearcarry();
    zerocalc(result);
    signcalc(result);
   
    putvalue(result);
}

static void rti() {
    status = pull8();
    value = pull16();
    pc = value;
}

static void rts() {
    value = pull16();
    pc = value + 1;
}

static void sbc() {
    penaltyop = 1;
    value = getvalue() ^ 0x00FF;
    result = (uint16_t)a + value + (uint16_t)(status & FLAG_CARRY);
   
    carrycalc(result);
    zerocalc(result);
    overflowcalc(result, a, value);
    signcalc(result);

    #ifndef NES_CPU
    if (status & FLAG_DECIMAL) {
        clearcarry();
        
        a -= 0x66;
        if ((a & 0x0F) > 0x09) {
            a += 0x06;
        }
        if ((a & 0xF0) > 0x90) {
            a += 0x60;
            setcarry();
        }
        
        clockticks6502++;
    }
    #endif
   
    saveaccum(result);
}

static void sec() {
    setcarry();
}

static void sed() {
    setdecimal();
}

static void sei() {
    setinterrupt();
}

static void sta() {
    putvalue(a);
}

static void stx() {
    putvalue(x);
}

static void sty() {
    putvalue(y);
}

static void stz() {
    putvalue(0);
}

static void tax() {
    x = a;
   
    zerocalc(x);
    signcalc(x);
}

static void tay() {
    y = a;
   
    zerocalc(y);
    signcalc(y);
}

static void tsx() {
    x = sp;
   
    zerocalc(x);
    signcalc(x);
}

static void txa() {
    a = x;
   
    zerocalc(a);
    signcalc(a);
}

static void txs() {
    sp = x;
}

static void tya() {
    a = y;
   
    zerocalc(a);
    signcalc(a);
}

static void wai() {
    //XXX
    while(1);
}

static void trb() {
    uint8_t test;
    uint8_t result;
    uint8_t value;

    value = getvalue();

    test = value & a;
    zerocalc(test);
    result = value & ~a;
    saveaccum(result);
}

static void tsb() {
    uint8_t test;
    uint8_t result;
    uint8_t value;

    value = getvalue();

    test = value & a;
    zerocalc(test);
    result = value | a;
    saveaccum(result);
}

static void rmb() {
    uint8_t bit;
    uint8_t value;
    uint8_t result;

    /* RMBX = 0x[0-7]7, so extract the bit to reset from the opcode. */
    bit = (opcode >> 8);
    value = getvalue();
    result = value & ~(1 << bit);

    saveaccum(result);
}

static void smb() {
    uint8_t bit;
    uint8_t value;
    uint8_t result;

    /* SMBX = 0x[8-F]7, so extract the bit to reset from the opcode. */
    bit = (opcode >> 8) - 8;
    value = getvalue();
    result = value | (1 << bit);

    saveaccum(result);
}

static void bbr() {
    uint8_t bit;
    uint8_t value;

    bit = (opcode >> 8);
    value = getvalue();

    if((value & bit) == 0) {
        oldpc = pc;
        pc += reladdr;
        if ((oldpc & 0xFF00) != (pc & 0xFF00)) clockticks6502 += 2; //check if jump crossed a page boundary
            else clockticks6502++;
    }
}

static void bbs() {
    uint8_t bit;
    uint8_t value;

    bit = (opcode >> 8);
    value = getvalue();

    if((value & bit) != 0) {
        oldpc = pc;
        pc += reladdr;
        if ((oldpc & 0xFF00) != (pc & 0xFF00)) clockticks6502 += 2; //check if jump crossed a page boundary
            else clockticks6502++;
    }
}

//undocumented instructions
#ifdef UNDOCUMENTED
    static void lax() {
        lda();
        ldx();
    }

    static void sax() {
        sta();
        stx();
        putvalue(a & x);
        if (penaltyop && penaltyaddr) clockticks6502--;
    }

    static void dcp() {
        dec();
        cmp();
        if (penaltyop && penaltyaddr) clockticks6502--;
    }

    static void isb() {
        inc();
        sbc();
        if (penaltyop && penaltyaddr) clockticks6502--;
    }

    static void slo() {
        asl();
        ora();
        if (penaltyop && penaltyaddr) clockticks6502--;
    }

    static void rla() {
        rol();
        and();
        if (penaltyop && penaltyaddr) clockticks6502--;
    }

    static void sre() {
        lsr();
        eor();
        if (penaltyop && penaltyaddr) clockticks6502--;
    }

    static void rra() {
        ror();
        adc();
        if (penaltyop && penaltyaddr) clockticks6502--;
    }
#else
    #define lax nop
    #define sax nop
    #define dcp nop
    #define isb nop
    #define slo nop
    #define rla nop
    #define sre nop
    #define rra nop
#endif

static void (*addr_handlers[NUM_ADDR_MODES])() = {
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

static uint8_t addr_lengths[NUM_ADDR_MODES] = {
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


#ifdef SUPPORT_65C02
static addr_mode_t addrtable[256] = {
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

static void (*optable[256])() = {
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

static const uint32_t ticktable[256] = {
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

static const char *mnemonics[256] = {
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
#else
static void (*addrtable[256])() = {
/*        |  0  |  1  |  2  |  3  |  4  |  5  |  6  |  7  |  8  |  9  |  A  |  B  |  C  |  D  |  E  |  F  |     */
/* 0 */     imp, indx,  imp, indx,   zp,   zp,   zp,   zp,  imp,  imm,  acc,  imm, abso, abso, abso, abso, /* 0 */
/* 1 */     rel, indy,  imp, indy,  zpx,  zpx,  zpx,  zpx,  imp, absy,  imp, absy, absx, absx, absx, absx, /* 1 */
/* 2 */    abso, indx,  imp, indx,   zp,   zp,   zp,   zp,  imp,  imm,  acc,  imm, abso, abso, abso, abso, /* 2 */
/* 3 */     rel, indy,  imp, indy,  zpx,  zpx,  zpx,  zpx,  imp, absy,  imp, absy, absx, absx, absx, absx, /* 3 */
/* 4 */     imp, indx,  imp, indx,   zp,   zp,   zp,   zp,  imp,  imm,  acc,  imm, abso, abso, abso, abso, /* 4 */
/* 5 */     rel, indy,  imp, indy,  zpx,  zpx,  zpx,  zpx,  imp, absy,  imp, absy, absx, absx, absx, absx, /* 5 */
/* 6 */     imp, indx,  imp, indx,   zp,   zp,   zp,   zp,  imp,  imm,  acc,  imm,  ind, abso, abso, abso, /* 6 */
/* 7 */     rel, indy,  imp, indy,  zpx,  zpx,  zpx,  zpx,  imp, absy,  imp, absy, absx, absx, absx, absx, /* 7 */
/* 8 */     imm, indx,  imm, indx,   zp,   zp,   zp,   zp,  imp,  imm,  imp,  imm, abso, abso, abso, abso, /* 8 */
/* 9 */     rel, indy,  imp, indy,  zpx,  zpx,  zpy,  zpy,  imp, absy,  imp, absy, absx, absx, absy, absy, /* 9 */
/* A */     imm, indx,  imm, indx,   zp,   zp,   zp,   zp,  imp,  imm,  imp,  imm, abso, abso, abso, abso, /* A */
/* B */     rel, indy,  imp, indy,  zpx,  zpx,  zpy,  zpy,  imp, absy,  imp, absy, absx, absx, absy, absy, /* B */
/* C */     imm, indx,  imm, indx,   zp,   zp,   zp,   zp,  imp,  imm,  imp,  imm, abso, abso, abso, abso, /* C */
/* D */     rel, indy,  imp, indy,  zpx,  zpx,  zpx,  zpx,  imp, absy,  imp, absy, absx, absx, absx, absx, /* D */
/* E */     imm, indx,  imm, indx,   zp,   zp,   zp,   zp,  imp,  imm,  imp,  imm, abso, abso, abso, abso, /* E */
/* F */     rel, indy,  imp, indy,  zpx,  zpx,  zpx,  zpx,  imp, absy,  imp, absy, absx, absx, absx, absx  /* F */
};

static void (*optable[256])() = {
/*        |  0  |  1  |  2  |  3  |  4  |  5  |  6  |  7  |  8  |  9  |  A  |  B  |  C  |  D  |  E  |  F  |      */
/* 0 */      brk,  ora,  nop,  slo,  nop,  ora,  asl,  slo,  php,  ora,  asl,  nop,  nop,  ora,  asl,  slo, /* 0 */
/* 1 */      bpl,  ora,  nop,  slo,  nop,  ora,  asl,  slo,  clc,  ora,  nop,  slo,  nop,  ora,  asl,  slo, /* 1 */
/* 2 */      jsr,  and,  nop,  rla,  bit,  and,  rol,  rla,  plp,  and,  rol,  nop,  bit,  and,  rol,  rla, /* 2 */
/* 3 */      bmi,  and,  nop,  rla,  nop,  and,  rol,  rla,  sec,  and,  nop,  rla,  nop,  and,  rol,  rla, /* 3 */
/* 4 */      rti,  eor,  nop,  sre,  nop,  eor,  lsr,  sre,  pha,  eor,  lsr,  nop,  jmp,  eor,  lsr,  sre, /* 4 */
/* 5 */      bvc,  eor,  nop,  sre,  nop,  eor,  lsr,  sre,  cli,  eor,  nop,  sre,  nop,  eor,  lsr,  sre, /* 5 */
/* 6 */      rts,  adc,  nop,  rra,  nop,  adc,  ror,  rra,  pla,  adc,  ror,  nop,  jmp,  adc,  ror,  rra, /* 6 */
/* 7 */      bvs,  adc,  nop,  rra,  nop,  adc,  ror,  rra,  sei,  adc,  nop,  rra,  nop,  adc,  ror,  rra, /* 7 */
/* 8 */      nop,  sta,  nop,  sax,  sty,  sta,  stx,  sax,  dey,  nop,  txa,  nop,  sty,  sta,  stx,  sax, /* 8 */
/* 9 */      bcc,  sta,  nop,  nop,  sty,  sta,  stx,  sax,  tya,  sta,  txs,  nop,  nop,  sta,  nop,  nop, /* 9 */
/* A */      ldy,  lda,  ldx,  lax,  ldy,  lda,  ldx,  lax,  tay,  lda,  tax,  nop,  ldy,  lda,  ldx,  lax, /* A */
/* B */      bcs,  lda,  nop,  lax,  ldy,  lda,  ldx,  lax,  clv,  lda,  tsx,  lax,  ldy,  lda,  ldx,  lax, /* B */
/* C */      cpy,  cmp,  nop,  dcp,  cpy,  cmp,  dec,  dcp,  iny,  cmp,  dex,  nop,  cpy,  cmp,  dec,  dcp, /* C */
/* D */      bne,  cmp,  nop,  dcp,  nop,  cmp,  dec,  dcp,  cld,  cmp,  nop,  dcp,  nop,  cmp,  dec,  dcp, /* D */
/* E */      cpx,  sbc,  nop,  isb,  cpx,  sbc,  inc,  isb,  inx,  sbc,  nop,  sbc,  cpx,  sbc,  inc,  isb, /* E */
/* F */      beq,  sbc,  nop,  isb,  nop,  sbc,  inc,  isb,  sed,  sbc,  nop,  isb,  nop,  sbc,  inc,  isb  /* F */
};

static const uint32_t ticktable[256] = {
/*        |  0  |  1  |  2  |  3  |  4  |  5  |  6  |  7  |  8  |  9  |  A  |  B  |  C  |  D  |  E  |  F  |     */
/* 0 */      7,    6,    2,    8,    3,    3,    5,    5,    3,    2,    2,    2,    4,    4,    6,    6,  /* 0 */
/* 1 */      2,    5,    2,    8,    4,    4,    6,    6,    2,    4,    2,    7,    4,    4,    7,    7,  /* 1 */
/* 2 */      6,    6,    2,    8,    3,    3,    5,    5,    4,    2,    2,    2,    4,    4,    6,    6,  /* 2 */
/* 3 */      2,    5,    2,    8,    4,    4,    6,    6,    2,    4,    2,    7,    4,    4,    7,    7,  /* 3 */
/* 4 */      6,    6,    2,    8,    3,    3,    5,    5,    3,    2,    2,    2,    3,    4,    6,    6,  /* 4 */
/* 5 */      2,    5,    2,    8,    4,    4,    6,    6,    2,    4,    2,    7,    4,    4,    7,    7,  /* 5 */
/* 6 */      6,    6,    2,    8,    3,    3,    5,    5,    4,    2,    2,    2,    5,    4,    6,    6,  /* 6 */
/* 7 */      2,    5,    2,    8,    4,    4,    6,    6,    2,    4,    2,    7,    4,    4,    7,    7,  /* 7 */
/* 8 */      2,    6,    2,    6,    3,    3,    3,    3,    2,    2,    2,    2,    4,    4,    4,    4,  /* 8 */
/* 9 */      2,    6,    2,    6,    4,    4,    4,    4,    2,    5,    2,    5,    5,    5,    5,    5,  /* 9 */
/* A */      2,    6,    2,    6,    3,    3,    3,    3,    2,    2,    2,    2,    4,    4,    4,    4,  /* A */
/* B */      2,    5,    2,    5,    4,    4,    4,    4,    2,    4,    2,    4,    4,    4,    4,    4,  /* B */
/* C */      2,    6,    2,    8,    3,    3,    5,    5,    2,    2,    2,    2,    4,    4,    6,    6,  /* C */
/* D */      2,    5,    2,    8,    4,    4,    6,    6,    2,    4,    2,    7,    4,    4,    7,    7,  /* D */
/* E */      2,    6,    2,    8,    3,    3,    5,    5,    2,    2,    2,    2,    4,    4,    6,    6,  /* E */
/* F */      2,    5,    2,    8,    4,    4,    6,    6,    2,    4,    2,    7,    4,    4,    7,    7   /* F */
};
#endif


void init6502(mem_space_t *_mem_space, bool reset)
{
    mem_space = _mem_space;

    if(reset)
        reset6502();
}

void nmi6502() {
    push16(pc);
    push8(status);
    status |= FLAG_INTERRUPT;
    pc = (uint16_t)mem_space->read(0xFFFA) | ((uint16_t)mem_space->read(0xFFFB) << 8);
}

void irq6502() {
    push16(pc);
    push8(status);
    status |= FLAG_INTERRUPT;
    pc = (uint16_t)mem_space->read(0xFFFE) | ((uint16_t)mem_space->read(0xFFFF) << 8);
}

uint8_t callexternal = 0;
void (*loopexternal)();

void exec6502(uint32_t tickcount) {
    clockgoal6502 += tickcount;
   
    while (clockticks6502 < clockgoal6502) {
        opcode = mem_space->read(pc++);
        status |= FLAG_CONSTANT;

        penaltyop = 0;
        penaltyaddr = 0;

        (*addr_handlers[addrtable[opcode]])();
        (*optable[opcode])();
        clockticks6502 += ticktable[opcode];
        if (penaltyop && penaltyaddr) clockticks6502++;

        instructions++;

        if (callexternal) (*loopexternal)();
    }

}

void step6502() {
//    if(pc == 0x81bc)
//        printf("fat_init\n");
//    else if(pc == 0x83bf)
//        printf("fat_open\n");
//    else if(pc == 0x8165)
//        printf("fat cache block\n");
//    else
//        printf("0x%04x\n", pc);
    opcode = mem_space->read(pc++);
    status |= FLAG_CONSTANT;

    penaltyop = 0;
    penaltyaddr = 0;

    (*addr_handlers[addrtable[opcode]])();
    (*optable[opcode])();
    clockticks6502 += ticktable[opcode];
    if (penaltyop && penaltyaddr) clockticks6502++;
    clockgoal6502 = clockticks6502;

    instructions++;

    if (callexternal) (*loopexternal)();
}

void hookexternal(void *funcptr) {
    if (funcptr != (void *)NULL) {
        loopexternal = funcptr;
        callexternal = 1;
    } else callexternal = 0;
}

void disassemble(size_t bufLen, char *buffer)
{
    const char *mn;
    addr_mode_t addr_mode;
    uint8_t opcode = mem_space->read(pc);
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
            snprintf(buffer, bufLen, " #$%02x", mem_space->read(pc+1));
            break;
        case ZP:
            snprintf(buffer, bufLen, " $00%02x", mem_space->read(pc+1));
            break;
        case ZPX:
            snprintf(buffer, bufLen, " $00%02x,X", mem_space->read(pc+1));
            break;
        case ZPY:
            snprintf(buffer, bufLen, " $00%02x,Y", mem_space->read(pc+1));
            break;
        case REL:
            snprintf(buffer, bufLen, " $%02x", mem_space->read(pc+1));
            break;
        case ABSO:
            snprintf(buffer, bufLen, " $%02x%02x", mem_space->read(pc+2), mem_space->read(pc+1));
            break;
        case ABSX:
            snprintf(buffer, bufLen, " $%02x%02x,X", mem_space->read(pc+2), mem_space->read(pc+1));
            break;
        case ABSY:
            snprintf(buffer, bufLen, " $%02x%02x,Y", mem_space->read(pc+2), mem_space->read(pc+1));
            break;
        case IND:
            snprintf(buffer, bufLen, " ($%02x%02x)", mem_space->read(pc+2), mem_space->read(pc+1));
            break;
        case INDX:
            snprintf(buffer, bufLen, " ($00%02x,X)", mem_space->read(pc+1));
            break;
        case INDY:
            snprintf(buffer, bufLen, " ($00%02x,Y)", mem_space->read(pc+1));
            break;
        case INDZ:
            snprintf(buffer, bufLen, " ($00%02x)", mem_space->read(pc+1));
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
    uint8_t opcode = mem_space->read(pc);
    return addr_lengths[addrtable[opcode]];
}

bool cpu_is_subroutine(void)
{
    uint8_t opcode = mem_space->read(pc);

    return opcode == 0x20;
}
