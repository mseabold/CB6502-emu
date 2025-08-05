.include "common.inc"
.code
    test_init

    sec
test_opcode:
    bcc branch
    nop
branch:

    test_complete


