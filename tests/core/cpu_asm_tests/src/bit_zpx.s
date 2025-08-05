.include "common.inc"
.code
    test_init

    ldx #0
test_opcode:
    bit zpval,X

    test_complete


