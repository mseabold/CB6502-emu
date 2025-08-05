.include "common.inc"
.code
    test_init

    clc
    lda #0
    stz zpval
test_opcode:
    adc zpval

    test_complete


