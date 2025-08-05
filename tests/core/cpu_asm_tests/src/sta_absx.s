.include "common.inc"
.code
    test_init

    ldx #5
    lda #0
test_opcode:
    sta bssval,X

    test_complete



