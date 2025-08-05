.include "common.inc"
.code
    test_init

    ldx #$aa
    txs
    lda #$ff
    sta TEST_CTRL
test_opcode:

    test_complete


