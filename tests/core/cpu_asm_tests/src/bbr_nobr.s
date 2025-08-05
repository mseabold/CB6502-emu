.include "common.inc"
.code
    test_init

    lda #$01
    sta zpval
test_opcode:
    bbr0 zpval, branch
    nop
branch:
    test_complete


