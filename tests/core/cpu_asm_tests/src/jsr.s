.include "common.inc"
.code
    test_init

test_opcode:
    jsr subroutine

    test_complete

subroutine:
    nop
    rts
