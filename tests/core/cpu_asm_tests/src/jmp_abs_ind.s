.include "common.inc"
.code
    test_init

   lda #<end
   sta bssptr
   lda #>end
   sta bssptr+1
test_opcode:
    jmp (bssptr)
    nop

end:
    test_complete


