#ifdef CURSES_WIDE_CHAR
#define _XOPEN_SOURCE_EXTENDED
#endif

#include <ncurses.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#ifdef CURSES_WIDE_CHAR
#include <locale.h>
#endif

#include "cursmgr.h"
#include "memwin.h"
#include "logwin.h"
#include "bpwin.h"
#include "regwin.h"
#include "codewin.h"
#include "log.h"


#define CORNER_TL L'\u250c'
#define CORNER_TR L'\u2510'
#define CORNER_BL L'\u2514'
#define CORNER_BR L'\u2518'
#define T_DOWN    L'\u252c'
#define T_RIGHT   L'\u251c'
#define T_LEFT    L'\u2524'
#define T_UP      L'\u2534'
#define CROSS     L'\u253c'
#define VERTLINE  L'\u2502'
#define HORLINE   L'\u2500'

#define SVERTLINE L"\u2502"
#define SHORLINE  L"\u2500"

typedef struct
{
    void *handle;
    WINDOW *window;
} window_handle_t;

typedef enum
{
    TOP_LEFT,
    TOP_RIGHT,
    BOTTOM_LEFT,
    BOTTOM_RIGHT
} corner_type_t;

typedef struct
{
    wchar_t oldchar;
    wchar_t newchar;
} corner_map_entry_t;

typedef struct
{
    unsigned int num_entries;
    const corner_map_entry_t *entries;
} corner_map_t;

#define NUM_ENTRIES(_map) (sizeof(_map)/sizeof(corner_map_entry_t))

const corner_map_entry_t topleft_map[] =
{
    { CORNER_TR, T_DOWN },
    { CORNER_BL, T_RIGHT },
    { CORNER_BR, CROSS },
    { T_UP, CROSS },
    { T_LEFT, CROSS },
    { VERTLINE, T_RIGHT },
    { HORLINE, T_DOWN }
};

const corner_map_entry_t topright_map[] =
{
    { CORNER_TL, T_DOWN },
    { CORNER_BL, CROSS },
    { CORNER_BR, T_LEFT },
    { T_RIGHT, CROSS },
    { T_UP, CROSS },
    { VERTLINE, T_LEFT },
    { HORLINE, T_DOWN }
};

const corner_map_entry_t bottomleft_map[] =
{
    { CORNER_TL, T_RIGHT },
    { CORNER_TR, CROSS },
    { CORNER_BR, T_UP },
    { T_DOWN, CROSS },
    { T_LEFT, CROSS },
    { VERTLINE, T_RIGHT },
    { HORLINE, T_UP }
};

const corner_map_entry_t bottomright_map[] =
{
    { CORNER_TL, CROSS },
    { CORNER_TR, T_LEFT },
    { CORNER_BL, T_UP },
    { T_DOWN, CROSS },
    { T_RIGHT, CROSS },
    { VERTLINE, T_LEFT },
    { HORLINE, T_UP }
};

const wchar_t default_corners[] =
{
    CORNER_TL,
    CORNER_TR,
    CORNER_BL,
    CORNER_BR
};

const corner_map_t corner_map[4] =
{
    { NUM_ENTRIES(topleft_map), topleft_map },
    { NUM_ENTRIES(topright_map), topright_map },
    { NUM_ENTRIES(bottomleft_map), bottomleft_map },
    { NUM_ENTRIES(bottomright_map), bottomright_map },
};

static const bool requires_debugger[] =
{
    false, /* REGISTERS */
    false, /* MEMORY */
    true,  /* CODE */
    true,  /* BREAKPOINT */
    false, /* LOG */
    false, /* CUSTOM */
};

static const char *default_labels[] =
{
    "Registers",
    "Memory",
    "Code",
    "Breakpoints",
    "Log",
    NULL
};

static sys_cxt_t sys;
static debug_t debugger;
static const window_info_t *windows;
static unsigned int num_windows;
window_handle_t *handles;

static void draw_line(int y, int x, int size, bool vert)
{
#ifdef CURSES_WIDE_CHAR
    cchar_t verline;
    cchar_t horline;

    setcchar(&verline, SVERTLINE, 0, 0, NULL);
    setcchar(&horline, SHORLINE, 0, 0, NULL);

    if(vert)
        mvvline_set(y, x, &verline, size);
    else
        mvhline_set(y, x, &horline, size);
#else
    if(vert)
        mvvline(y, x, '|', size);
    else
        mvhline(y, x, '-', size);
#endif
}

#define draw_hline(_y, _x, _size) draw_line(_y, _x, _size, false)
#define draw_vline(_y, _x, _size) draw_line(_y, _x, _size, true)

static void draw_corner(corner_type_t type, int y, int x)
{
#ifdef CURSES_WIDE_CHAR
    cchar_t c;
    wchar_t wstr[16];
    attr_t attrs;
    short colorpair;
    const corner_map_t *map;
    unsigned int index;
    wchar_t new;

    move(y, x);

    in_wch(&c);

    log_print(lDEBUG, "num wchars: %d\n", getcchar(&c, NULL, &attrs, &colorpair, NULL));
    getcchar(&c, wstr, &attrs, &colorpair, NULL);

    map = &corner_map[type];

    for(index = 0; index < map->num_entries; ++ index)
    {
        if(map->entries[index].oldchar == wstr[0])
        {
            break;
        }
    }

    if(index == map->num_entries)
    {
        /* No map entry. If this is currently a space,
         * add the default corner. Otherwise, make no change
         */
        if(wstr[0] == ' ')
        {
            new = default_corners[type];
        }
        else
        {
            new = wstr[0];
        }
    }
    else
    {
        new = map->entries[index].newchar;
    }

    wstr[0] = new;
    wstr[1] = 0;

    setcchar(&c, wstr, 0, 0, NULL);

    add_wch(&c);
#else
    mvaddch(y, x, '+');
#endif
}

static void draw_border(const window_info_t *window)
{
    draw_hline(window->y, window->x + 1, window->width - 2);
    draw_vline(window->y + 1, window->x, window->height-2);
    draw_vline(window->y + 1, window->x + window->width -1, window->height-2);
    draw_hline(window->y + window->height - 1, window->x + 1, window->width - 2);
}

static void draw_corners(const window_info_t *window)
{
    draw_corner(TOP_LEFT, window->y, window->x);
    draw_corner(TOP_RIGHT, window->y, window->x + window->width - 1);
    draw_corner(BOTTOM_LEFT, window->y + window->height -1, window->x);
    draw_corner(BOTTOM_RIGHT, window->y + window->height - 1, window->x + window->width - 1);
}

static void draw_label(const window_info_t *window)
{
    const char *label = window->label;

    if(label == NULL)
    {
        label = default_labels[window->type];
    }

    if(label == NULL)
    {
        return;
    }

    /* Make sure the label + some space padding + some border padding will fit
     * inside the window width. */
    if(strlen(label) + 4 > window->width)
    {
        return;
    }

    mvprintw(window->y, window->x+2, " %s ", label);
}

static bool check_overlap(const window_info_t *w1, const window_info_t *w2)
{
    log_print(lDEBUG, "check_overlap: w1: (%u, %u %ux%u), w2: (%u, %u, %ux%u)\n", w1->y, w1->x, w1->height, w1->width, w2->y, w2->x, w2->height, w2->width);

    /* Allow borders/padding to overlap. */
    if(w1->y >= w2->y + w2->height - 1 || w2->y >= w1->y + w1->height - 1)
        return false;

    if(w1->x >= w2->x + w2->width - 1|| w2->x >= w1->x + w1->width - 1)
        return false;

    return true;
}

void cursmgr_init(sys_cxt_t _sys, debug_t _debugger, unsigned int *height, unsigned int *width)
{
#ifdef CURSES_WIDE_CHAR
    setlocale(LC_ALL, "");
#endif

    initscr();
    cbreak();
    noecho();
    curs_set(0);

    sys = _sys;
    debugger = _debugger;

    if(height)
        *height = LINES;

    if(width)
        *width = COLS;

    log_print(lDEBUG, "Window Size: %d, %d\n", LINES, COLS);
}

cursmgr_status_t cursmgr_run(unsigned int _num_windows, const window_info_t *_windows)
{
    unsigned int index;
    unsigned int index2;
    cursmgr_status_t status;
    int c;
    bool exit;

    num_windows = _num_windows;
    windows = _windows;

    if(num_windows == 0 || windows == NULL || sys == NULL)
    {
        return ERR_INVALID_PARAMETER;
    }

    /* First, validate all the windows. */
    for(index = 0; index < num_windows; ++index)
    {
        const window_info_t *window = &windows[index];

        /* Make sure the type is valid so we can safely use it for lookup tables. */
        /* TODO Once custom windows have an interface, change this to > */
        if(window->type >= CUSTOM)
        {
            return ERR_INVALID_PARAMETER;
        }

        /* Make sure the window fits on the screen. */
        if(window->x >= COLS || window->y >= LINES
            || window->x + window->width > COLS || window->y + window->height > LINES
            || window->height == 0 || window->width == 0)
        {
            return ERR_WINDOW_DOES_NOT_FIT;
        }

        if(requires_debugger[window->type] && debugger == NULL)
        {
            return ERR_DEBUGGER_REQUIRED;
        }

        if(window->type == CODE)
        {
            if(window->parameters != NULL)
            {
                int bpindex = ((codewin_params_t *)window->parameters)->breakpoint_window_index;

                log_print(lDEBUG, "bpindex: %d\n", bpindex);

                if(bpindex < -1 || bpindex >= (int)num_windows)
                {
                    return ERR_INVALID_INDEX;
                }
            }
            else
            {
                return ERR_INVALID_PARAMETER;
            }
        }

        /* Look for any window overlaps. Since this is a communative check, we only need to check
         * between this window and later indexes, as checks with earlier indexes have already been
         * performed. */
        for(index2 = index + 1; index2 < num_windows; ++index2)
        {
            if(check_overlap(window, &windows[index2]))
            {
                return ERR_WINDOW_OVERLAP;
            }
        }
    }

    /* Windows are OK. Allocate an array to hold handles/runtime info for the windows. */
    handles = malloc(num_windows * sizeof(window_handle_t));

    if(handles == NULL)
    {
        return ERR_MEMORY;
    }

    status = USER_EXIT;

    for(index = 0; index < num_windows && status == USER_EXIT; ++index)
    {
        const window_info_t *window = &windows[index];
        window_handle_t *handle = &handles[index];

        handle->window = newwin(window->height-2, window->width-2, window->y + 1, window->x + 1);

        if(handle->window == NULL)
        {
            status = ERR_ON_WINDOW_INIT;
            break;
        }

        if(!(window->flags & NO_BORDER))
        {
            draw_border(window);
        }

        refresh();

        switch(window->type)
        {
            case REGISTERS:
                handle->handle = regwin_init(handle->window);
                break;
            case MEMORY:
                handle->handle = memwin_init(handle->window, sys);
                break;
            case CODE:
                handle->handle = codewin_create(handle->window, debugger, ((codewin_params_t *)window->parameters)->num_dbginfo, ((codewin_params_t *)window->parameters)->dbginfo);
                break;
            case BREAKPOINT:
                handle->handle = bpwin_init(handle->window, debugger);
                break;
            case LOG:
                /* There is no dynamic context so there is no handle */
                curses_logwin_init(handle->window);
                break;
            case CUSTOM:
                /* Not currently supported. */
                break;

        }

        /* Log window doesn't have handle. Otherwise fail out if a handle was not created. */
        if(window->type != LOG && handles->handle == NULL)
        {
            status = ERR_ON_WINDOW_INIT;
        }
    }

    /* One more pass to populate the corners of the borders and labels as well
     * as handle some parameter mapping*/
    for(index = 0; index < num_windows; ++index)
    {
        const window_info_t *window = &windows[index];
        int idx;

        if(!(window->flags & NO_BORDER))
        {
            draw_corners(window);
        }

        if(!(window->flags & NO_LABEL))
        {
            draw_label(window);
        }

        if(window->type == CODE)
        {
            idx = ((codewin_params_t *)window->parameters)->breakpoint_window_index;

            if(idx != -1)
                codewin_set_bpwin((codewin_t)handles[index].handle, (bpwin_t)handles[idx].handle);
        }
    }

    exit = false;

    while(!exit)
    {
        c = getch();

        for(index = 0; index < num_windows; ++index)
        {
            switch(windows[index].type)
            {
                case MEMORY:
                    memwin_processchar((memwin_t)handles[index].handle, c);
                    break;
                case CODE:
                    codewin_processchar((codewin_t)handles[index].handle, c);
                    break;
                case BREAKPOINT:
                    bpwin_processchar((bpwin_t)handles[index].handle, c);
                    break;
                case CUSTOM:
                    break;
                default:
                    break;
            }
        }

        for(index = 0; index < num_windows; ++index)
        {
            switch(windows[index].type)
            {
                case REGISTERS:
                    regwin_refresh((regwin_t)handles[index].handle);
                    break;
                case MEMORY:
                    memwin_refresh((memwin_t)handles[index].handle);
                    break;
                case CUSTOM:
                    break;
                default:
                    break;
            }
        }

        if(c == 'q')
        {
            exit = true;
        }
    }

    /* Free the context for all the windows. */
    for(index = 0; index < num_windows; ++index)
    {
        if(handles[index].window != NULL)
            delwin(handles[index].window);

        if(handles[index].handle != NULL)
        {
            switch(windows[index].type)
            {
                case REGISTERS:
                    regwin_destroy((regwin_t)handles[index].handle);
                    break;
                case MEMORY:
                    memwin_destroy((memwin_t)handles[index].handle);
                    break;
                case CODE:
                    codewin_destroy((codewin_t)handles[index].handle);
                    break;
                case BREAKPOINT:
                    bpwin_destroy((bpwin_t)handles[index].handle);
                    break;
                case LOG:
                    /* No destroy atm. */
                    break;
                case CUSTOM:
                    /* Currently unsupported */
                    break;
                default:
                    break;
            }
        }
    }

    free(handles);

    return status;
}

void cursmgr_cleanup(void)
{
    endwin();
}
