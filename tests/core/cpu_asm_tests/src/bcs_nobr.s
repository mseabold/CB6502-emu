.include "common.inc"
.code
    test_init

    clc
test_opcode:
    bcs branch
    nop
branch:

    test_complete
