.include "common.inc"

PAGEEND = __SCRATCH_START__ + $ff
.code
    test_init

    lda #0
    ldx #1
    stz PAGEEND+1
    sec
test_opcode:
    sbc PAGEEND,X

    test_complete

