.include "common.inc"
.code
    test_init

    stz bssval
    ldy #0
    sec
test_opcode:
    sbc bssval,Y

    test_complete


