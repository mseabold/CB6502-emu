.include "common.inc"
.code
    test_init

    ldy #0
test_opcode:
    and bssval,Y

    test_complete


