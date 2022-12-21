#ifndef __DEBUGWIN_H__
#define __DEBUGWIN_H__

#include <ncurses.h>

#include "debugger.h"

typedef struct debugwin_s *debugwin_t;

debugwin_t debugwin_create(WINDOW *curswindow, debug_t debugger, int height, int width);
void debugwin_destroy(debugwin_t window);
void debugwin_processchar(debugwin_t window, char input);

#endif /* end of include guard: __DEBUGWIN_H__ */
