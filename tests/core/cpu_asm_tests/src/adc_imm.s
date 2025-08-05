.include "common.inc"
.code
    test_init

    lda #0
    clc
test_opcode:
    adc #1

    test_complete


