.include "common.inc"

PAGEEND = __SCRATCH_START__ + $ff
.code
    test_init

    ldy #1
    stz PAGEEND+1
test_opcode:
    lda PAGEEND,Y

    test_complete

