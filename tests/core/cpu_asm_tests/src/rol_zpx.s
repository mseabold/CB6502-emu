.include "common.inc"
.code
    test_init

    ldx #0
    stz zpval
test_opcode:
    rol zpval,X

    test_complete


