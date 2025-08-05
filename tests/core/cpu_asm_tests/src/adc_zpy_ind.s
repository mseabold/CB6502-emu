.include "common.inc"
.code
    test_init

    lda #<bssval
    sta zpptr
    lda #>bssval
    sta zpptr+1
    ldy #0
    clc
test_opcode:
    adc (zpptr),Y

    test_complete



