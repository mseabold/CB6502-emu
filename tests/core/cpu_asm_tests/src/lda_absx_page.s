.include "common.inc"

PAGEEND = __SCRATCH_START__ + $ff
.code
    test_init

    ldx #1
    stz PAGEEND+1
test_opcode:
    lda PAGEEND,X

    test_complete

