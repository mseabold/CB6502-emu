.include "common.inc"
.code
    test_init

    ldy #0
    stz bssval
test_opcode:
    ldx bssval,Y

    test_complete


