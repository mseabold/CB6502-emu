.include "common.inc"
.code
    test_init

    stz bssval
    ldx #0
    sec
test_opcode:
    sbc bssval,X

    test_complete

