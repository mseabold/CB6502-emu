.include "common.inc"
.code
    test_init

    stz bssval
    ldx #0
    lda #0
test_opcode:
    adc bssval,X

    test_complete



