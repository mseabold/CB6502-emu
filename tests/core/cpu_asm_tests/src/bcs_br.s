.include "common.inc"
.code
    test_init

    sec
test_opcode:
    bcs branch
    nop
branch:

    test_complete

