.include "common.inc"
.code
    test_init

    stz bssval
    ldy #0
    lda #0
test_opcode:
    adc bssval,Y

    test_complete


