.include "common.inc"
.code
    test_init

    stz zpval
    ldx #0
    lda #0
test_opcode:
    adc zpval,X

    test_complete


