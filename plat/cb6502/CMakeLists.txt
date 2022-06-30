add_executable(cb6502
    cb6502.c
    bitbang_spi.c)

target_link_libraries(cb6502 cbemu sdcard at28c256 dbgcli)
