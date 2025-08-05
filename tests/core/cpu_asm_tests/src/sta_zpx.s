.include "common.inc"
.code
    test_init

    lda #1
    ldx #0
test_opcode:
    sta zpval,X

    test_complete


