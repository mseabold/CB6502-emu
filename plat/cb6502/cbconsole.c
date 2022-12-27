#include <curses.h>
#include <stdio.h>
#include <string.h>

#include "cpu.h"
#include "acia.h"
#include "cb6502.h"
#include "debugger.h"
#include "debugwin.h"
#include "log.h"
#include "logwin.h"
#include "bpwin.h"
#include "regwin.h"

#define CHAR_QUIT 'q'

extern WINDOW *linedebugwin;

static void draw_box(int y, int x, int height, int width, char *label, bool draw_bottom)
{
    mvaddch(y,x,'+');
    mvaddch(y,x+width-1,'+');
    mvvline(y+1,x,'|',height-2);
    mvvline(y+1,x+width-1,'|',height-2);
    mvhline(y,x+1,'=',width-2);

    if(draw_bottom)
    {
        mvhline(y+height-1,x+1,'=',width-2);
        mvaddch(y+height-1,x,'+');
        mvaddch(y+height-1,x+width-1,'+');
    }

    if(label && strlen(label) < width+2)
    {
        mvprintw(y,x+2," %s ",label);
    }
}
int main(int argc, char *argv[])
{
    int i;
    char disbuf[128];
    uint16_t pc;
    debugwin_t dbgwin;
    WINDOW *win;
    WINDOW *logwin;
    debug_t debugger;
    bool done;
    int c;
    int dbgwidth;
    int dbgheight;
    bpwin_t bpwin;
    regwin_t regwin;

    if(argc < 2)
    {
        fprintf(stderr, "Usage: %s rom_file\n", argv[0]);
        return 1;
    }

    if(!cb6502_init(argv[1], ACIA_DEFAULT_SOCKNAME))
        return 1;

    initscr();
    cbreak();
    noecho();
    curs_set(0);

    debugger = debug_init(cb6502_get_sys());

    dbgwidth = COLS/2;
    dbgheight = LINES-8;

    draw_box(0,0,dbgheight+1,dbgwidth+1, "Disassembly", false);
    draw_box(dbgheight, 0, LINES-dbgheight, dbgwidth+1, "Breakpoints", true);
    draw_box(0,dbgwidth,5,COLS-dbgwidth, "Registers", false);
    draw_box(4, dbgwidth, LINES-4, COLS-dbgwidth, "Log", true);
    refresh();

    logwin = newwin(LINES-6, COLS-dbgwidth-2, 5, dbgwidth+1);
#if 1
    curses_logwin_init(logwin);
    log_set_handler(curses_logwin_print);
#else
    linedebugwin = logwin;
#endif
    log_set_level(lDEBUG);
    refresh();
    win = newwin(dbgheight-1, dbgwidth-2, 1, 1);
    refresh();
    dbgwin = debugwin_create(win, debugger);

    win = newwin(LINES-dbgheight-2, dbgwidth-1, dbgheight+1, 1);
    refresh();
    bpwin = bpwin_init(win, debugger);

    win = newwin(3, COLS-dbgwidth-2, 1, dbgwidth+1);
    regwin = regwin_init(win);

    debugwin_set_bpwin(dbgwin, bpwin);

    done = false;

    while(!done)
    {
        c = getch();

        if(c != CHAR_QUIT)
        {
            debugwin_processchar(dbgwin, c);
            bpwin_processchar(bpwin, c);
        }
        else
        {
            done = true;
        }

        regwin_refresh(regwin);
    }

    endwin();

    debugwin_destroy(dbgwin);
    cb6502_destroy();

    return 0;
}
