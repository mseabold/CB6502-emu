.include "common.inc"
.code
    test_init

    lda #<isr
    sta $fffe
    lda #>isr
    sta $ffff

    ldx #0
    cli
loop:
    nop
    nop
    bra loop

test_opcode:

    test_complete


isr:
    nop
    nop
    rti

