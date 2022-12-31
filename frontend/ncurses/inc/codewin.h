#ifndef __DISASSEMBLYWIN_H__
#define __DISASSEMBLYWIN_H__

#include <ncurses.h>

#include "debugger.h"
#include "bpwin.h"
#include "dbginfo.h"

typedef struct codewin_s *codewin_t;

typedef struct
{
    cc65_dbginfo handle;

    uint32_t valid_options;
    const char *source_prefix;
    const char *source_replace;
} codewin_dbginfo_t;

#define DBGOPT_SOURCE_PREFIX    0x00000001
#define DBGOPT_SOURCE_REPLACE   0x00000002

codewin_t codewin_create(WINDOW *curswindow, debug_t debugger, unsigned int num_dbginfo, const codewin_dbginfo_t *dbginfo);
void codewin_destroy(codewin_t window);
void codewin_processchar(codewin_t window, char input);
void codewin_set_bpwin(codewin_t window, bpwin_t breakpoint_window);

#endif /* end of include guard: __DISASSEMBLYWIN_H__ */
