.include "common.inc"
.code
    test_init
    lda #<isr
    sta $fffe
    lda #>isr
    sta $ffff

test_opcode:
    brk
    nop
    test_complete
isr:
    nop
    php
    rti

