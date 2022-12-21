find_package(Curses REQUIRED)

add_library(fe_ncurses STATIC
    src/debugwin.c
    src/logwin.c
)

target_include_directories(fe_ncurses
    PUBLIC
        inc
        ${CURSES_INCLUDE_DIR}
)

target_link_libraries(fe_ncurses
    PUBLIC
        cbemu
        ${CURSES_LIBRARY}
)

