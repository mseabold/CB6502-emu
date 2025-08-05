.include "common.inc"
.code
    test_init

    ldx #1
    ldy #0
test_opcode:
    stx zpval,Y

    test_complete


