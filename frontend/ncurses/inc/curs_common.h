#ifndef __CURS_COMMON_H__
#define __CURS_COMMON_H__

#if defined(CURSES_HAVE_NCURSES_H)
#include <ncurses.h>
#elif defined(CURSES_HAVE_NCURSES_NCURSES_H)
#include <ncurses/ncurses.h>
#elif defined(CURSES_HAVE_CURSES_H)
#include <curses.h>
#elif defined(CURSES_HAVE_NCURSES_CURSES_H)
#include <ncurses/curses.h>
#else
#error "Unable to determine location of ncurses header"
#endif

#endif /* end of include guard: __CURS_COMMON_H__ */
