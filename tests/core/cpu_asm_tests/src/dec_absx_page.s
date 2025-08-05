.include "common.inc"

PAGEEND = __SCRATCH_START__ + $ff
.code
    test_init

    ldx #1
test_opcode:
    dec PAGEEND,X

    test_complete
