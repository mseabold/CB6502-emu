#include <stdlib.h>
#include <string.h>

#include "tracewin.h"

struct tracewin_s
{
    WINDOW *curswin;
    sys_cxt_t sys;
    sys_trace_cb_t cb_handle;
};

typedef struct tracewin_s *tracewin_cxt_t;

void trace_callback(uint16_t addr, uint8_t value, bool write, void *param)
{
    tracewin_cxt_t handle = (tracewin_cxt_t)param;
    wprintw(handle->curswin, "%c 0x%04x 0x%02x\n", write ? 'W' : 'R', addr, value);

    /* Note that we are explicitly not redrawing the window here. If the UI performs
     * a "next" over a long subroutine, this could bog the whole system down. Instead,
     * let the refresh handler do it. This means we could lose a lot of information,
     * but if it's necessary to see, you can always step through the routine.
     */
}

tracewin_t tracewin_init(WINDOW *curswin, void *params)
{
    tracewin_cxt_t handle;

    if(params == NULL)
    {
        return NULL;
    }

    handle = malloc(sizeof(struct tracewin_s));

    if(handle == NULL)
    {
        return NULL;
    }

    handle->curswin = curswin;
    handle->sys = ((tracewin_params_t *)params)->sys;
    handle->cb_handle = sys_register_mem_trace_callback(handle->sys, trace_callback, handle);

    if(handle->cb_handle == NULL)
    {
        free(handle);
        return NULL;
    }

    scrollok(handle->curswin, true);
    return handle;
}

void tracewin_refresh(tracewin_t window)
{
    tracewin_cxt_t handle = (tracewin_cxt_t)window;

    wrefresh(handle->curswin);
}

void tracewin_destroy(tracewin_t window)
{
    tracewin_cxt_t handle = (tracewin_cxt_t)window;

    sys_un_register_mem_trace_callback(handle->sys, handle->cb_handle);
    free(handle);
}
