.include "common.inc"
.code
    test_init

    lda #0
    cmp #1
test_opcode:
    bpl end
    nop
end:
    test_complete


