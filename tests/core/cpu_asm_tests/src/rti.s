.include "common.inc"
.code
    test_init

    lda #<isr
    sta $fffe
    lda #>isr
    sta $ffff
    cli
    brk
    nop
    test_complete
isr:
    nop
test_opcode:
    rti


