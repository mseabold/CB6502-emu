#ifndef __MEMWIN_H__
#define __MEMWIN_H__

#include <curses.h>

#include "sys.h"

typedef void *memwin_t;

typedef struct
{
    sys_cxt_t sys;
} memwin_params_t;

memwin_t memwin_init(WINDOW *curswin, void *params);
void memwin_refresh(memwin_t window);
void memwin_processchar(memwin_t window, int input);
void memwin_destroy(memwin_t window);

#endif /* end of include guard: __MEMWIN_H__ */
