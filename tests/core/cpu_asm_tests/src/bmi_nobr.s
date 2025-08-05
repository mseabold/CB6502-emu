.include "common.inc"
.code
    test_init

    lda #0
test_opcode:
    bmi branch
    nop

branch:
    test_complete


