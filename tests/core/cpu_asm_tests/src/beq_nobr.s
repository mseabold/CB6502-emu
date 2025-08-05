.include "common.inc"
.code
    test_init

    lda #0
    cmp #1
test_opcode:
    beq branch
    nop
branch:

    test_complete


