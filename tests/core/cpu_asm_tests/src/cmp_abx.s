.include "common.inc"
.code
    test_init

    ldx #0
test_opcode:
    cmp bssval,X

    test_complete
