MEMORY {
    ZP: start = $0000, size = $0100;
    SCRATCH: start = $3000, size = $0100, define = YES;
    TEST: start = $2000, size = $0200, define = YES;
}

SEGMENTS {
    ZEROPAGE: load = ZP, run = ZP, type = zp;
    CODE: load = TEST, type = ro;
    BSS: load = SCRATCH, type = bss;
}

SYMBOLS {
    TEST_CTRL: type = export, value = $aaaa;
}
