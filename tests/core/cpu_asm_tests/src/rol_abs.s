.include "common.inc"
.code
    test_init

    stz bssval
test_opcode:
    rol bssval

    test_complete


