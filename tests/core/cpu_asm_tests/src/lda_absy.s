.include "common.inc"
.code
    test_init

    ldy #0
    stz bssval
test_opcode:
    lda bssval,Y

    test_complete


