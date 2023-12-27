option(CURSES_USE_WIDE "Use wide characters in ncurses" ON)

if(CURSES_USE_WIDE)
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

if(CURSES_USE_WIDE)
    set(CURSES_DIR "ncursesw")
else()
    set(CURSES_DIR "ncurses")
endif()

if(CURSES_HAVE_NCURSES_NCURSES_H)
    set(CURSES_HEADER "<${CURSES_DIR}/ncurses.h>")
elseif(CURSES_HAVE_NCURSES_H)
    set(CURSES_HEADER "<ncurses.h>")
elseif(CURSES_HAVE_CURSES_H)
    set(CURSES_HEADER "<curses.h>")
elseif(CURSES_HAVE_NCURSES_CURSES_H)
    set(CURSES_HEADER "<${CURSES_DIR}/curses.h")
else()
    # If NEEDS_WIDE is TRUE, FindCurses will skip looking for ncurses.h without
    # an ncursesw/ directory. However I have seen distros (Arch at least) where ncurses with
    # unicode support still only has ncurses.h. Default to that if we can't find anything else
    set(CURSES_HEADER "<ncurses.h>")
endif()

target_compile_definitions(fe_ncurses PUBLIC
    CURSES_HEADER=${CURSES_HEADER}
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
