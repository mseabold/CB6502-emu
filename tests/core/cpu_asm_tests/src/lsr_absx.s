.include "common.inc"
.code
    test_init

    stz bssval
    lda #0
    ldx #0
test_opcode:
    lsr bssval,X

    test_complete


