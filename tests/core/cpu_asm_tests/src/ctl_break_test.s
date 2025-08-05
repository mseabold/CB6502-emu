.include "common.inc"
.code
    test_init

    nop
    nop

    lda #$ff
    sta TEST_CTRL

test_opcode:

    test_complete


