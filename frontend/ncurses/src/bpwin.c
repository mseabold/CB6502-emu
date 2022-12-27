#include <curses.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "bpwin.h"
#include "debugger.h"
#include "log.h"

#define INPUT_BUFFER_SIZE 64

struct bpwin_s
{
    WINDOW *curswin;
    debug_t debugger;
    int height;
    uint32_t active_bp;
    uint32_t num_bps;
    char input_buffer[INPUT_BUFFER_SIZE];
    unsigned int input_index;

    breakpoint_info_t *bp_info;
};

static void refresh_win(bpwin_t handle)
{
    uint32_t index;
    uint32_t num_bps;

    num_bps = handle->height - 1;

    debug_get_breakpoints(handle->debugger, &num_bps, handle->bp_info, NULL);

    handle->num_bps = num_bps;

    wmove(handle->curswin, 0, 0);

    for(index = 0; index < handle->num_bps; ++index)
    {
        if(handle->bp_info[index].handle == handle->active_bp)
        {
            log_print(lDEBUG, "refresh(): %u is active\n", index);
            wattron(handle->curswin, A_REVERSE);
        }
        else
        {
            log_print(lDEBUG, "refresh(): %u is not active\n", index);
            wattroff(handle->curswin, A_REVERSE);
        }

        wprintw(handle->curswin, "#%2u: 0x%04x\n", handle->bp_info[index].handle, handle->bp_info[index].address);
    }

    attroff(A_REVERSE);

    for(index = 0; index < handle->height - 1; ++index)
    {
        wprintw(handle->curswin, "\n");
    }
    wrefresh(handle->curswin);
}

bpwin_t bpwin_init(WINDOW *curswin, debug_t debugger)
{
    int height, width;
    bpwin_t handle;

    getmaxyx(curswin, height, width);

    handle = malloc(sizeof(struct bpwin_s));
    memset(handle, 0, sizeof(struct bpwin_s));

    if(handle == NULL)
        return NULL;

    handle->bp_info = malloc(sizeof(breakpoint_info_t) * (height - 1));

    if(handle->bp_info == NULL)
    {
        free(handle);
        return NULL;
    }

    handle->curswin = curswin;
    handle->debugger = debugger;
    handle->height = height;
    handle->active_bp = (uint32_t)-1;

    return handle;
}

static void get_input(bpwin_t handle)
{
    int ch;
    int y;
    int x;

    while(((ch = getch()) != '\n') && handle->input_index < INPUT_BUFFER_SIZE-1)
    {
        log_print(lDEBUG, "read ch: %d, bs: %d, bs_ascii: %d\n", ch, KEY_BACKSPACE, '\b');

        if(ch == '\b' || ch == KEY_BACKSPACE || ch == 127)
        {
            if(handle->input_index > 0)
            {
                --handle->input_index;
                wprintw(handle->curswin, "\b \b");
            }

            wrefresh(handle->curswin);
        }
        else
        {
            handle->input_buffer[handle->input_index++] = ch;
            waddch(handle->curswin, ch);
            wrefresh(handle->curswin);
        }
    }

    handle->input_buffer[handle->input_index] = '\0';
}

void bpwin_processchar(bpwin_t window, int input)
{
    char *endptr;
    long ival;
    debug_breakpoint_t bp;

    switch(input)
    {
        case 'b':
            window->input_index = 0;
            wmove(window->curswin, window->height - 1, 0);
            wprintw(window->curswin, "b ");
            wrefresh(window->curswin);
            get_input(window);
            wmove(window->curswin, window->height - 1, 0);
            wprintw(window->curswin, "\n");

            endptr = NULL;

            ival = strtol(window->input_buffer, &endptr, 16);

            if(endptr != NULL && *endptr == '\0' && ival >= 0 && ival < 0x10000)
            {
                /* Whole string is valid 16 bit hex integer. */
                debug_set_breakpoint_addr(window->debugger, &bp, (uint16_t)ival);
                refresh_win(window);
            }
            else
            {
                wmove(window->curswin, window->height - 1, 0);
                wprintw(window->curswin, "Invalid address input\n");
                wrefresh(window->curswin);

                sleep(1);

                wmove(window->curswin, window->height - 1, 0);
                wprintw(window->curswin, "\n");
                wrefresh(window->curswin);
            }

            break;
        case 'd':
            window->input_index = 0;
            wmove(window->curswin, window->height - 1, 0);
            wprintw(window->curswin, "d ");
            wrefresh(window->curswin);
            get_input(window);
            wmove(window->curswin, window->height - 1, 0);
            wprintw(window->curswin, "\n");

            endptr = NULL;

            ival = strtol(window->input_buffer, &endptr, 16);

            if(endptr != NULL && *endptr == '\0' && ival >= 0 && ival < 0x10000)
            {
                debug_clear_breakpoint(window->debugger, (debug_breakpoint_t)ival);
                refresh_win(window);
            }
            else
            {
                wmove(window->curswin, window->height - 1, 0);
                wprintw(window->curswin, "Invalid breakpoint handle\n");
                wrefresh(window->curswin);

                sleep(1);

                wmove(window->curswin, window->height - 1, 0);
                wprintw(window->curswin, "\n");
                wrefresh(window->curswin);
            }
        default:
            break;
    }
}

void bpwin_set_active_bp(bpwin_t window, uint32_t bp)
{
    log_print(lDEBUG, "Set %u bp active\n", bp);
    window->active_bp = bp;
    refresh_win(window);
}

void bpwin_clear_active_bp(bpwin_t window)
{
    window->active_bp = (uint32_t)-1;
    refresh_win(window);
}

void bpwin_destroy(bpwin_t window)
{
    free(window->bp_info);
    free(window);
}
