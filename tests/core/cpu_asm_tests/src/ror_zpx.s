.include "common.inc"
.code
    test_init

    stz zpval
    ldx #0
test_opcode:
    ror zpval,X

    test_complete


