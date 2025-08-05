.include "common.inc"
.code
    test_init

    stz bssval
    lda #0
test_opcode:
    eor bssval

    test_complete


