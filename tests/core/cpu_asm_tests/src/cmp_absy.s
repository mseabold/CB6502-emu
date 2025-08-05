.include "common.inc"
.code
    test_init

    ldy #0
test_opcode:
    cmp bssval,Y

    test_complete
