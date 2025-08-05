.include "common.inc"
.code
    test_init

    lda #<bssval
    sta zpptr
    lda #>bssval
    sta zpptr+1
    ldx #0
    lda #$55
test_opcode:
    sta (zpptr,X)

    test_complete


