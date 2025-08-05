.include "common.inc"
.code
    test_init

    sec
    lda #0
    sbc #1
test_opcode:
    bmi branch
    nop
branch:

    test_complete


