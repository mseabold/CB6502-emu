#include <curses.h>
#include <stdlib.h>
#include <string.h>

#include "memwin.h"
#include "log.h"

#define IS_HEX(c) (((c) >= '0' && (c) <= '9') || ((c) >= 'a' && (c) <= 'f') || ((c) >= 'A' && (c) <= 'F'))

struct memwin_s
{
    WINDOW *curswin;
    sys_cxt_t sys;
    uint16_t curbase;
    int height;
    int width;

    bool search_active;
    char search_str[4];
};

static int hexchar_to_int(char hexchar)
{
    if(hexchar >= 'a')
    {
        hexchar -= 32;
    }

    if(hexchar >= 'A')
        return 10 + (hexchar - 'A');
    else
        return hexchar - '0';
}

memwin_t memwin_init(WINDOW *curswin, sys_cxt_t sys)
{
    memwin_t handle;

    handle = malloc(sizeof(struct memwin_s));

    if(handle == NULL)
        return NULL;

    memset(handle, 0, sizeof(struct memwin_s));

    handle->curswin = curswin;
    handle->sys = sys;
    handle->curbase = 0;

    getmaxyx(curswin, handle->height, handle->width);

    memwin_refresh(handle);

    return handle;
}

void memwin_refresh(memwin_t window)
{
    int index;
    int index2;
    int endbyte;

    wmove(window->curswin, 0, 0);

    /* TODO smaller lines for small windows? */
    wprintw(window->curswin, "     0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F\n");

    endbyte = window->curbase + ((window->height-1) * 16);

    for(index = window->curbase; index < endbyte; ++index)
    {
        if((index & 0x000F) == 0)
        {
            wprintw(window->curswin, "%03X ", (index >> 4));
        }

        wprintw(window->curswin, "%02X ", sys_peek_mem(window->sys, index));

        if((index & 0x000F) == 0x0F)
        {
            wprintw(window->curswin, "\n");
        }
    }

    if(window->search_active)
    {
        mvwprintw(window->curswin, window->height-1, window->width-6, "/%s\n", window->search_str);
    }

    wrefresh(window->curswin);
}

void memwin_processchar(memwin_t window, int input)
{
    int c;
    int nibnum = 0;
    uint16_t tmpbase = window->curbase;
    int nibval;

    if(input == '/')
    {
        memset(window->search_str, 0, sizeof(window->search_str));
        window->search_active = true;

        memwin_refresh(window);

        while(nibnum < 3 && ((c = getch()) != '\n'))
        {
            if(!IS_HEX(c))
            {
                break;
            }

            window->search_str[nibnum] = c;
            nibval = hexchar_to_int(c);

            if(nibnum == 0)
                window->curbase = 0;

            window->curbase |= (nibval << (4 * (3-nibnum)));

            log_print(lDEBUG, "memwin: get nim %d. val %04x\n", nibnum, window->curbase);

            memwin_refresh(window);

            ++nibnum;
        }

        if(nibnum < 3)
        {
            window->curbase = tmpbase;
        }

        window->search_active = false;
        memwin_refresh(window);
    }
}

void memwin_destroy(memwin_t window)
{
    free(window);
}

