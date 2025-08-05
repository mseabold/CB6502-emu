.include "common.inc"
.code
    test_init

    stz bssval
    sec
test_opcode:
    sbc bssval

    test_complete


