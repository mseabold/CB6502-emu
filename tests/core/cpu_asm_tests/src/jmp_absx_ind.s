.include "common.inc"
.code
    test_init

    lda #<done
    sta bssptr
    lda #>done
    sta bssptr+1
    ldx #0
test_opcode:
    jmp (bssptr,X)
    nop

done:
    test_complete


