.include "common.inc"
.code
    test_init

    ldx #2
test_opcode:
    ora (zpptr, X)

    test_complete


