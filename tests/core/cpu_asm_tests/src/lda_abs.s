.include "common.inc"
.code
    test_init

    stz bssval
test_opcode:
    lda bssval

    test_complete


