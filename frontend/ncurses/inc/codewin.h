#ifndef __DISASSEMBLYWIN_H__
#define __DISASSEMBLYWIN_H__

#include <ncurses.h>

#include "debugger.h"
#include "bpwin.h"
#include "dbginfo.h"

/**
 * Code window handle.
 */
typedef void *codewin_t;

/**
 * Debug Information for the Code Window.
 */
typedef struct
{
    /**
     * Handle to information loaded from a cc65 debug file
     */
    cc65_dbginfo handle;

    /**
     * Bitmask representing which options were supplied by the user.
     */
    uint32_t valid_options;

    /**
     * String representing a path that should be prefixed to any source file
     * names in the debug information.
     *
     * Example:
     *
     * source_prefix = /home/user/sbc6502
     * dbginfo path = src/source.s
     * opened file: /home/user/sbc6502/src/source.s
     */
    const char *source_prefix;

    /**
     * String representing a substring that, if found in a source file name,
     * shoule be replaced. It will either by removed entirely or replaced
     * by the contents of source_prefix.
     *
     * Example:
     *
     * source_replace = /home/user/cb6502/
     * dbginfo path = /home/user/cb6502/src/source.s
     * opened file = src/source.s
     */
    const char *source_replace;
} codewin_dbginfo_t;

#define DBGOPT_SOURCE_PREFIX    0x00000001
#define DBGOPT_SOURCE_REPLACE   0x00000002

typedef struct
{
    debug_t debugger;
    unsigned int num_dbginfo;
    const codewin_dbginfo_t *dbginfo;
} codewin_params_t;

/**
 * Create a new code window context. While currently it only supports a single debug information structure being
 * supplied, the API supports future expansion. This would cover the use case of separate debug information
 * for ROM code and a RAM application.
 *
 * @param[in] curswindow    The ncurses window that will be used for the display
 * @param[in] debugger      The emulator debugger context that will be used for controlling execution
 * @param[in] num_dbginfo   The number of debug information structures being supplied. Currently only 1 is supported.
 * @param[in] dbginfo       The debug information to use by the code window. If NULL is supplied, the window will
 *                          default to only using direct disassembly.
 *
 * @return The newly created code window context, or NULL if unable to be created.
 */
codewin_t codewin_init(WINDOW *curswindow, void *params);

/**
 * Destroy a previously created code window context.
 *
 * @param[in] window    The previously allocated code window context.
 */
void codewin_destroy(codewin_t window);

/**
 * Provided an input character to the code window for processing.
 *
 * @param[in] window    The code window context
 * @param[in] input     The input character to process.
 */
void codewin_processchar(codewin_t window, int input);

/**
 * Provide the handle to a breakpoint window for the code window to reference.
 * If the code window has a refernce to a breakpoint window, it will set a breakpoint
 * to be highlighted when encountered by the debugger context.
 */
void codewin_set_bpwin(codewin_t window, bpwin_t breakpoint_window);

#endif /* end of include guard: __DISASSEMBLYWIN_H__ */
