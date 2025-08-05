.include "common.inc"
.code
    test_init

    lda #<bssval
    sta zpptr
    lda #>bssval
    sta zpptr+1
    ldx #0
test_opcode:
    and (zpptr,X)

    test_complete


