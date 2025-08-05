.include "common.inc"
.code
    test_init

    stz zpval
test_opcode:
    ora zpval

    test_complete


