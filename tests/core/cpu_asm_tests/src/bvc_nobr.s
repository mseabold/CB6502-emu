.include "common.inc"
.code
    test_init

    clc
    lda #$50
    adc #$50
test_opcode:
    bvc branch
    nop

branch:
    test_complete


