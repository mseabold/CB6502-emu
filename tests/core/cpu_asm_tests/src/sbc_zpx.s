.include "common.inc"
.code
    test_init

    stz zpval
    ldx #0
    sec
test_opcode:
    sbc zpval,X

    test_complete


