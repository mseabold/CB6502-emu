.include "common.inc"
.code
    test_init

    stz bssval
    ldx #0
test_opcode:
    ror bssval,X

    test_complete




