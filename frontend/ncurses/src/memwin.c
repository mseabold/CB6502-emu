#include "curs_common.h"
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

typedef struct memwin_s *memwin_cxt_t;

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

memwin_t memwin_init(WINDOW *curswin, void *p)
{
    memwin_cxt_t handle;
    memwin_params_t *params = (memwin_params_t *)p;

    handle = malloc(sizeof(struct memwin_s));

    if(handle == NULL)
        return NULL;

    memset(handle, 0, sizeof(struct memwin_s));

    handle->curswin = curswin;
    handle->sys = params->sys;
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
    memwin_cxt_t handle = (memwin_cxt_t)window;

    wmove(handle->curswin, 0, 0);

    /* TODO smaller lines for small handles? */
    wprintw(handle->curswin, "     0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F\n");

    endbyte = handle->curbase + ((handle->height-1) * 16);

    for(index = handle->curbase; index < endbyte; ++index)
    {
        if((index & 0x000F) == 0)
        {
            wprintw(handle->curswin, "%03X ", (index >> 4));
        }

        wprintw(handle->curswin, "%02X ", sys_peek_mem(handle->sys, index));

        if((index & 0x000F) == 0x0F)
        {
            wprintw(handle->curswin, "\n");
        }
    }

    if(handle->search_active)
    {
        mvwprintw(handle->curswin, handle->height-1, handle->width-6, "/%s\n", handle->search_str);
    }

    wrefresh(handle->curswin);
}

void memwin_processchar(memwin_t window, int input)
{
    memwin_cxt_t handle = (memwin_cxt_t)window;
    int c;
    int nibnum = 0;
    uint16_t tmpbase = handle->curbase;
    int nibval;

    if(input == '/')
    {
        memset(handle->search_str, 0, sizeof(handle->search_str));
        handle->search_active = true;

        memwin_refresh(handle);

        while(nibnum < 3 && ((c = getch()) != '\n'))
        {
            if(!IS_HEX(c))
            {
                break;
            }

            handle->search_str[nibnum] = c;
            nibval = hexchar_to_int(c);

            if(nibnum == 0)
                handle->curbase = 0;

            handle->curbase |= (nibval << (4 * (3-nibnum)));

            log_print(lDEBUG, "memwin: get nim %d. val %04x\n", nibnum, handle->curbase);

            memwin_refresh(handle);

            ++nibnum;
        }

        if(nibnum < 3)
        {
            handle->curbase = tmpbase;
        }

        handle->search_active = false;
        memwin_refresh(handle);
    }
}

void memwin_destroy(memwin_t window)
{
    free(window);
}

