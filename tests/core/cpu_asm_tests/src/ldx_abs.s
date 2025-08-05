.include "common.inc"
.code
    test_init

    stz bssval
test_opcode:
    ldx bssval

    test_complete


