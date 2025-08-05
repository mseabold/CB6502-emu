.include "common.inc"
.code
    test_init

    lda #<bssval
    sta zpptr
    lda #>bssval
    sta zpptr+1
test_opcode:
    cmp (zpptr)

    test_complete


