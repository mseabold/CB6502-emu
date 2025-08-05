.include "common.inc"

PAGEEND = __SCRATCH_START__ + $ff
.code
    test_init

    lda #0
    ldy #1
test_opcode:
    sta PAGEEND,Y

    test_complete





