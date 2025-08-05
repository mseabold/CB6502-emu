.include "common.inc"
.code
    test_init

    ldx #0
test_opcode:
    asl zpval,X

    test_complete


