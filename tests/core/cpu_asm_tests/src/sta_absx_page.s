.include "common.inc"

PAGEEND = __SCRATCH_START__ + $ff
.code
    test_init

    lda #0
    ldx #1
test_opcode:
    sta PAGEEND,X

    test_complete
