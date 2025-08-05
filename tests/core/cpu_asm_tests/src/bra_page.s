.include "common.inc"

TEST_DST = __SCRATCH_START__ + 253
.code
    test_init_no_opaddr

    lda #<TEST_DST
    sta opaddr
    lda #>TEST_DST
    sta opaddr+1

    memcpy test_data, TEST_DST, test_data_end-test_data

    jmp TEST_DST
end:
    test_complete

test_data:
    bra branch
    nop
branch:
    jmp end
test_data_end:



