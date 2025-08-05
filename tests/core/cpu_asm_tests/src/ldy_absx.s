.include "common.inc"
.code
    test_init

    ldx #0
    stz bssval
test_opcode:
    ldy bssval,X

    test_complete


