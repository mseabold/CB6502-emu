.include "common.inc"
.code
    test_init

    lda #<bssval
    sta zpptr
    lda #>bssval
    sta zpptr+1
    ldx #0
    stz bssval
    lda #0
    clc
test_opcode:
    adc (zpptr,X)

    test_complete


