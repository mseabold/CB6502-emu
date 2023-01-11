#ifndef __TRACEWIN_H__
#define __TRACEWIN_H__

#include <curses.h>

#include "sys.h"

typedef void *tracewin_t;

typedef struct
{
    sys_cxt_t sys;
} tracewin_params_t;

tracewin_t tracewin_init(WINDOW *curswin, void *params);
void tracewin_refresh(tracewin_t window);
void tracewin_destroy(tracewin_t window);

#endif /* end of include guard: __TRACEWIN_H__ */
