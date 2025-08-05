.include "common.inc"
.code
    test_init

    ldy #0
    stz zpval
test_opcode:
    ldx zpval,Y

    test_complete


