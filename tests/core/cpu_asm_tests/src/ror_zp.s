.include "common.inc"
.code
    test_init

    stz zpval
    lda #0
test_opcode:
    ror zpval

    test_complete


