.include "common.inc"
.code
    test_init

    lda #<bssval
    sta zpptr
    lda #>bssval
    sta zpptr+1
    ldy #0
    stz bssval
    sec
test_opcode:
    sbc (zpptr),Y

    test_complete


