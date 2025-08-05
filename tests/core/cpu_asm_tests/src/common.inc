; vim: set syntax=asm_ca65:
.import TEST_CTRL

.import __SCRATCH_START__

.define TEST_CTRL_COMPLETE $55

.macro test_complete
    lda #TEST_CTRL_COMPLETE
    sta TEST_CTRL
.endmacro

.macro op_addr
    lda #<test_opcode
    sta opaddr
    lda #>test_opcode
    sta opaddr+1
.endmacro

.macro test_init_no_opaddr
    sei
    cld
    ldx #$ff
    txs
.endmacro
.macro test_init
    sei
    cld
    ldx #$ff
    txs
    op_addr
.endmacro

.macro memcpy src, dst, len
    ldx #len
@loop:
    lda src,X
    sta dst,X
    dex
    bpl @loop
.endmacro

.zeropage
opaddr: .res 2
zpval: .res 1
zpptr: .res 2

.bss
bssptr: .res 2
bssval: .res 1
