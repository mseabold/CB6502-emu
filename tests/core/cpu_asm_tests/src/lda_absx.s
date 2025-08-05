.include "common.inc"
.code
    test_init

    ldx #0
    stz bssval
test_opcode:
    lda bssval,X

    test_complete

