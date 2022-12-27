#ifndef __BPWIN_H__
#define __BPWIN_H__

#include <curses.h>

#include "debugger.h"

typedef struct bpwin_s *bpwin_t;

bpwin_t bpwin_init(WINDOW *curswin, debug_t debugger);
void bpwin_processchar(bpwin_t window, int input);
void bpwin_set_active_bp(bpwin_t window, uint32_t bp);
void bpwin_clear_active_bp(bpwin_t window);
void bpwin_destroy(bpwin_t window);

#endif /* end of include guard: __BPWIN_H__ */
