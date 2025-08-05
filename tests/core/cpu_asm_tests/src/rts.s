.include "common.inc"
.code
    test_init

    jsr subroutine

    test_complete
subroutine:
    nop
test_opcode:
    rts

