.include "common.inc"
.code
    test_init

    stz bssval
test_opcode:
    ldy bssval

    test_complete


