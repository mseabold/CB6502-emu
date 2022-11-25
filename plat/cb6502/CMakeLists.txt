add_library(cb6502 STATIC
    cb6502.c
    bitbang_spi.c)

target_link_libraries(cb6502 PUBLIC
    cbemu
    sdcard
    at28c256
)

add_executable(cbdbg cbdbg.c)

target_link_libraries(cbdbg dbgcli cb6502)

find_package(Curses REQUIRED)

add_executable(cbconsole cbconsole.c)

target_include_directories(cbconsole
     PRIVATE
        ${CURSES_INCLUDE_DIR})

target_link_libraries(cbconsole
    PRIVATE
        cb6502
        ${CURSES_LIBRARY})

