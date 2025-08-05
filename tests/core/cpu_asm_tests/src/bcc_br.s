.include "common.inc"
.code
    test_init

    clc
test_opcode:
    bcc branch
    nop
branch:

    test_complete


