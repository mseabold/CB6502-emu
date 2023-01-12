option(CURSES_USE_WIDE "Use wide characters in ncurses" ON)

find_package(Curses REQUIRED)

if(${CURSES_FOUND})

add_library(fe_ncurses STATIC
    src/codewin.c
    src/logwin.c
    src/bpwin.c
    src/regwin.c
    src/memwin.c
    src/tracewin.c
    src/cursmgr.c
)

target_include_directories(fe_ncurses
    PUBLIC
        inc
        ${CURSES_INCLUDE_DIR}
)

target_link_libraries(fe_ncurses
    PUBLIC
        cbemu
        dbginfo
)

# The documentation seems to indicate that setting CURSES_NEED_WIDE to TRUE
# would cause CURSES_LIBRARIES to be populated accordingly. However, this
# did not work for me initially. Also, NEED_WIDE requires 3.10+.
# instead, just link the right library directly based on our own option
if(${CURSES_USE_WIDE})
    target_link_libraries(fe_ncurses
        PUBLIC
            ncursesw
    )

    target_compile_definitions(fe_ncurses
        PRIVATE
            CURSES_WIDE_CHAR
    )
else()
    target_link_libraries(fe_ncurses
        PUBLIC
            curses
    )
endif()

endif()
