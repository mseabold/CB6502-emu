.include "common.inc"
.code
    test_init

    lda #1
    sta zpval
test_opcode:
    bbs0 zpval,branch
    nop
branch:
    test_complete
