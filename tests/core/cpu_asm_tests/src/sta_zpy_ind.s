.include "common.inc"
.code
    test_init

    lda #<bssval
    sta zpptr
    lda #>bssval
    sta zpptr+1
    ldy #0
    lda #1
test_opcode:
    sta (zpptr),Y

    test_complete


