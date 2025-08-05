.include "common.inc"
.code
    test_init

test_opcode:
    jmp end

    nop
    nop
end:
    test_complete


