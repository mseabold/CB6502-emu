.include "common.inc"

PAGEEND = __SCRATCH_START__ + $ff
.code
    test_init

    lda #<PAGEEND
    sta zpptr
    lda #>PAGEEND
    sta zpptr+1
    ldy #1
    stz PAGEEND+1
test_opcode:
    eor (zpptr),Y

    test_complete


