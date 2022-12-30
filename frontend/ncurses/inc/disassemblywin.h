#ifndef __DEBUGWIN_H__
#define __DEBUGWIN_H__

#include <ncurses.h>

#include "debugger.h"
#include "bpwin.h"

typedef struct debugwin_s *debugwin_t;

debugwin_t debugwin_create(WINDOW *curswindow, debug_t debugger);
void debugwin_destroy(debugwin_t window);
void debugwin_processchar(debugwin_t window, char input);
void debugwin_set_bpwin(debugwin_t window, bpwin_t breakpoint_window);

#endif /* end of include guard: __DEBUGWIN_H__ */
