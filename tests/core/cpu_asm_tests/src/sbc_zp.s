.include "common.inc"
.code
    test_init

    stz zpval
    sec
test_opcode:
    sbc zpval

    test_complete


