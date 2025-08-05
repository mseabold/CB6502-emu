.include "common.inc"
.code
    test_init

    ldy #1
    ldx #0
test_opcode:
    sty zpval,X

    test_complete


