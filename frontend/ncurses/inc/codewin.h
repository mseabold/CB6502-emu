#ifndef __DISASSEMBLYWIN_H__
#define __DISASSEMBLYWIN_H__

#include <ncurses.h>

#include "debugger.h"
#include "bpwin.h"

typedef struct disasswin_s *disasswin_t;

disasswin_t disasswin_create(WINDOW *curswindow, debug_t debugger);
void disasswin_destroy(disasswin_t window);
void disasswin_processchar(disasswin_t window, char input);
void disasswin_set_bpwin(disasswin_t window, bpwin_t breakpoint_window);

#endif /* end of include guard: __DISASSEMBLYWIN_H__ */
