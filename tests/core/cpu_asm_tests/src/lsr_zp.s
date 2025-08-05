.include "common.inc"
.code
    test_init

    stz zpval
    lda #0
test_opcode:
    lsr zpval

    test_complete


