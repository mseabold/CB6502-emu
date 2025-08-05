.include "common.inc"
.code
    test_init

    lda #$55
test_opcode:
    asl

    test_complete


