.include "common.inc"
.code
    test_init

    lda #<bssval
    sta zpptr
    lda #>bssval
    sta zpptr+1
    stz bssval
    lda #0
test_opcode:
    eor (zpptr)

    test_complete



