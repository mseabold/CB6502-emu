.include "common.inc"
.code
    test_init

    lda #<bssval
    sta zpptr
    lda #>bssval
    sta zpptr+1
    stz bssval
    ldy #0
    lda #0
test_opcode:
    eor (zpptr),Y

    test_complete


