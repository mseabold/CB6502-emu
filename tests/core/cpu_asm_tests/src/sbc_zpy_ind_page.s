.include "common.inc"

PAGEEND = __SCRATCH_START__ + $ff
.code
    test_init

    lda #<PAGEEND
    sta zpptr
    lda #>PAGEEND
    sta zpptr+1
    ldy #1
    stz bssval
    sec
test_opcode:
    sbc (zpptr),Y

    test_complete
