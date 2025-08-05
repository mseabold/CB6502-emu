.include "common.inc"
.code
    test_init

    lda #1
    stz bssval
test_opcode:
    lsr bssval

    test_complete


