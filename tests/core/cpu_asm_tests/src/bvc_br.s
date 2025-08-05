.include "common.inc"
.code
    test_init

    clc
    lda #0
    adc #0
test_opcode:
    bvc branch
    nop
branch:
    test_complete


