#ifndef __CURSMGR_H__
#define __CURSMGR_H__

#include <curses.h>
#include <stdbool.h>

#include "sys.h"
#include "debugger.h"
#include "codewin.h"

/**
 * Enumeration of the types of supported windows by the curses manager
 */
typedef enum
{
    REGISTERS, /**< Register watch window */
    MEMORY,     /**< Memory dump window */
    CODE,       /**< Code/disassembly window */
    BREAKPOINT, /**< Breakpoint list window */
    LOG,        /**< Text log window */
    TRACE,      /**< Memory bus trace window */
    CUSTOM,     /**< Custom application-defined window. (Not currently supported yet) */
} window_type_t;

/**
 * Additional manager-specific layer of parameters specifically for codewins that allows
 * the user to supply a breakpoint window to link to the code window.
 */
typedef struct
{
    /**
     * Window in the window list given to cursmgr_run for a breakpoint window to link to.
     * -1 indicates that there is no breakpoint window to link.
     */
    int breakpoint_window;
} cursmgr_codewin_params_t;

/**
 * Definition for a curses window to be displayed by the curses manager
 */
typedef struct
{
    /**
     * The type of the widow.
     */
    window_type_t type;

    /**
     * The y position of the top-left corner of the window.
     */
    unsigned int y;

    /**
     * The x position of the top-left corner of the window.
     */
    unsigned int x;

    /**
     * The height of the window
     */
    unsigned int height;

    /**
     * The width of the window
     */
    unsigned int width;

    /**
     * The label of the window to draw in the top border. If this is NULL,
     * the default label for the window type will be used. Custom windows
     * have no default label.
     */
    const char *label;

    /**
     * Option flags for the window
     */
    uint32_t flags;

    /**
     * Parameters for the curses manager. There are window-specific, but internal to the
     * manager and not passed to the window itself.
     */
    void *parameters;

    /**
     * Window-specific parameters that will be passed to the window manager when it is
     * initialized.
     */
    void *window_parameters;
} window_info_t;

/**
 * Do not draw a border around the window. One cell of padding is still applied
 * in lew of the border.
 */
#define NO_BORDER   0x00000001

/**
 * Do not add a label to the window.
 */
#define NO_LABEL    0x00000002

typedef enum
{
    USER_EXIT,
    ERR_WINDOW_DOES_NOT_FIT,
    ERR_WINDOW_OVERLAP,
    ERR_ON_WINDOW_INIT,
    ERR_DEBUGGER_REQUIRED,
    ERR_INVALID_INDEX,
    ERR_INVALID_PARAMETER,
    ERR_MEMORY,
} cursmgr_status_t;


/**
 * Initializes the curses manager and prepares the terminal for curses mode.
 *
 * @param[out] height   If supplied, will be populated with the height of the terminal screen.
 * @param[out] width    If supplied, will be populated with the width of the terminal screen.
 */
void cursmgr_init(unsigned int *height, unsigned int *width);

/**
 * Initializes and draw the supplied windows and starts the curses manager main loop
 *
 * @param[in] num_windows The number of windows supplied to draw
 * @param[in] windows     The windows to draw to the screen
 *
 * @return Status of the curses main loop that causes it to return
 */
cursmgr_status_t cursmgr_run(unsigned int num_windows, const window_info_t *windows);

/**
 * Cleans up the curses manager and restores the terminal settings from curses mode
 */
void cursmgr_cleanup(void);

#endif /* end of include guard: __CURSMGR_H__ */
