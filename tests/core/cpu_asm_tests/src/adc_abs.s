.include "common.inc"
.code
    test_init

    clc
    stz bssval
    lda #0
test_opcode:
    adc bssval

    test_complete


