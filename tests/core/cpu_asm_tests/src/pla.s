.include "common.inc"
.code
    test_init

    lda #$55
    pha
test_opcode:
    pla

    test_complete


