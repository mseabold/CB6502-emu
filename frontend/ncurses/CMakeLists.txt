set(CURSES_NEED_WIDE TRUE)
find_package(Curses REQUIRED)

add_library(fe_ncurses STATIC
    src/codewin.c
    src/logwin.c
    src/bpwin.c
    src/regwin.c
    src/memwin.c
)

target_include_directories(fe_ncurses
    PUBLIC
        inc
        ${CURSES_INCLUDE_DIR}
)

target_link_libraries(fe_ncurses
    PUBLIC
        cbemu
        cursesw
        dbginfo
)

