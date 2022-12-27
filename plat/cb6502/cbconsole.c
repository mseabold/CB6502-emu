#define _XOPEN_SOURCE_EXTENDED
#include <ncurses.h>
#include <stdio.h>
#include <string.h>

#include <locale.h>

#include "cpu.h"
#include "acia.h"
#include "cb6502.h"
#include "debugger.h"
#include "debugwin.h"
#include "log.h"
#include "logwin.h"
#include "bpwin.h"
#include "regwin.h"
#include "memwin.h"
#include "syslog_log.h"

#define CHAR_QUIT 'q'

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

extern WINDOW *linedebugwin;

static void draw_box(int y, int x, int height, int width, char *label, bool draw_bottom, wchar_t tl, wchar_t tr, wchar_t bl, wchar_t br)
{
    cchar_t verline;
    cchar_t horline;

    setcchar(&verline, SVERTLINE, 0, 0, NULL);
    setcchar(&horline, SHORLINE, 0, 0, NULL);
    mvvline_set(y+1,x,&verline,height-2);
    mvvline_set(y+1,x+width-1,&verline,height-2);
    mvhline_set(y,x+1,&horline,width-2);

    if(tl == 0)
        tl = CORNER_TL;

    if(tr == 0)
        tr = CORNER_TR;

    if(bl == 0)
        bl = CORNER_BL;

    if(br == 0)
        br = CORNER_BR;

    mvprintw(y,x,"%lc",tl);
    mvprintw(y,x+width-1,"%lc",tr);
    if(draw_bottom)
    {
        mvhline_set(y+height-1,x+1,&horline,width-2);
        mvprintw(y+height-1,x,"%lc",bl);
        mvprintw(y+height-1,x+width-1,"%lc",br);
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
    memwin_t memwin;

    if(argc < 2)
    {
        fprintf(stderr, "Usage: %s rom_file\n", argv[0]);
        return 1;
    }

    if(!cb6502_init(argv[1], ACIA_DEFAULT_SOCKNAME))
        return 1;

    setlocale(LC_ALL, "");
    initscr();
    cbreak();
    noecho();
    curs_set(0);

    debugger = debug_init(cb6502_get_sys());

    dbgwidth = COLS/2;
    dbgheight = LINES-8;

    draw_box(0,0,dbgheight+1,dbgwidth+1, "Disassembly", false, 0, T_DOWN, 0, 0);
    draw_box(dbgheight, 0, LINES-dbgheight, dbgwidth+1, "Breakpoints", true, T_RIGHT, T_LEFT, 0, 0);
    draw_box(0,dbgwidth,5,COLS-dbgwidth, "Registers", false, T_DOWN, 0, T_RIGHT, T_LEFT);
    draw_box(4, dbgwidth, LINES-4, COLS-dbgwidth, "Memory", true, T_RIGHT, T_LEFT, T_UP, 0);
    refresh();

    logwin = newwin(LINES-6, COLS-dbgwidth-2, 5, dbgwidth+1);
#if 1
    //curses_logwin_init(logwin);
    log_set_handler(syslog_log_print);
#else
    linedebugwin = logwin;
#endif
    log_set_level(lDEBUG);
    refresh();
    memwin = memwin_init(logwin, cb6502_get_sys());
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
            memwin_processchar(memwin, c);
        }
        else
        {
            done = true;
        }

        regwin_refresh(regwin);
        memwin_refresh(memwin);
    }

    endwin();

    debugwin_destroy(dbgwin);
    cb6502_destroy();

    return 0;
}
