.include "common.inc"
.code
    test_init

    stz bssval
    lda #<bssval
    sta zpptr
    lda #>bssval
    sta zpptr+1
    clc
test_opcode:
    adc (zpptr)

    test_complete


