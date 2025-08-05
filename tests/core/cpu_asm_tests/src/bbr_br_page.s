.include "common.inc"

TEST_DST = __SCRATCH_START__ + 252
.code
    test_init_no_opaddr

    memcpy test_data, TEST_DST, test_data_end-test_data

    lda #<TEST_DST
    sta opaddr
    lda #>TEST_DST
    sta opaddr+1

    stz zpval
    jmp TEST_DST
end:
    test_complete

test_data:
    bbr0 zpval, branch
    nop
branch:
    jmp end
test_data_end:
