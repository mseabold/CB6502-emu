.include "common.inc"
.code
    test_init

    ldy #0
    lda #0
test_opcode:
    sta bssval,Y

    test_complete


