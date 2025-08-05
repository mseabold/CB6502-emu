.include "common.inc"
.code
    test_init

    lda #0
    cmp #0
test_opcode:
    beq branch
    nop
branch:

    test_complete


