.include "common.inc"
.code
    test_init

test_opcode:
    bra branch
    nop
branch:

    test_complete


