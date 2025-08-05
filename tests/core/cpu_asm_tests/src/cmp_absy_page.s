.include "common.inc"

PAGEEND = __SCRATCH_START__ + $ff
.code
    test_init

    ldy #1
test_opcode:
    cmp PAGEEND,Y

    test_complete

