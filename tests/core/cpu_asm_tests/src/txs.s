.include "common.inc"
.code
    test_init

    ldx #$ff
test_opcode:
    txs

    test_complete


