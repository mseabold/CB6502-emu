.include "common.inc"
.code
    test_init

    stz zpval
test_opcode:
    bbs0 zpval,branch
    nop
branch:
    test_complete
