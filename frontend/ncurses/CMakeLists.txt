option(CURSES_USE_WIDE "Use wide characters in ncurses" ON)

if(${CURSES_USE_WIDE})
    set(CURSES_NEED_WIDE TRUE)
endif()

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

if(CURSES_HAVE_NCURSES_H)
    set(CURSES_HEADER_DEF "CURSES_HAVE_NCURSES_H")
elseif(CURSES_HAVE_NCURSES_NCURSES_H)
    set(CURSES_HEADER_DEF "CURSES_HAVE_NCURSES_NCURSES_H")
elseif(CURSES_HAVE_CURSES_H)
    set(CURSES_HEADER_DEF "CURSES_HAVE_CURSES_H")
elseif(CURSES_HAVE_NCURSES_CURSES_H)
    set(CURSES_HEADER_DEF "CURSES_HAVE_NCURSES_CURSES_H")
else()
    # I've seen this happen if you use NEED_WIDE but there is no
    # ncursesw/ directory in the include path. Fallback to just using
    # ncurses.h
    set(CURSES_HEADER_DEF "CURSES_HAVE_NCURSES_H")
endif()

target_compile_definitions(fe_ncurses PUBLIC
    ${CURSES_HEADER_DEF}
)

# The documentation seems to indicate that setting CURSES_NEED_WIDE to TRUE
# would cause CURSES_LIBRARIES to be populated accordingly. However, this
# did not work for me initially. Also, NEED_WIDE requires 3.10+.
# instead, just link the right library directly based on our own option
    target_link_libraries(fe_ncurses
        PUBLIC
            ${CURSES_LIBRARIES}
    )

if(${CURSES_USE_WIDE})
    target_compile_definitions(fe_ncurses
        PRIVATE
            CURSES_WIDE_CHAR
    )
endif()

endif()
