.include "common.inc"
.code
    test_init

    lda #<bssval
    sta zpptr
    lda #>bssval
    sta zpptr+1
    ldy #0
test_opcode:
    and (zpptr),Y

    test_complete


