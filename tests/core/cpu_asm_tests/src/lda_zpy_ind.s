.include "common.inc"
.code
    test_init

    lda #<bssval
    sta zpptr
    lda #>bssval
    sta zpptr+1
    ldy #0
    stz bssval
test_opcode:
    lda (zpptr),Y

    test_complete



