#include <curses.h>
#include <stdio.h>

#include "cpu.h"
#include "acia.h"
#include "cb6502.h"
#include "debugger.h"
#include "debugwin.h"
#include "log.h"
#include "logwin.h"

#define CHAR_QUIT 'q'

extern WINDOW *linedebugwin;
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
    char c;
    int dbgwidth;

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

    logwin = newwin(LINES, COLS-dbgwidth, 0, dbgwidth);
#if 1
    curses_logwin_init(logwin);
    log_set_handler(curses_logwin_print);
#else
    linedebugwin = logwin;
#endif
    log_set_level(lDEBUG);
    refresh();
    win = newwin(LINES, dbgwidth, 0, 0);
    refresh();
    dbgwin = debugwin_create(win, debugger, LINES, COLS);

    done = false;

    while(!done)
    {
        c = getch();

        if(c != CHAR_QUIT)
        {
            debugwin_processchar(dbgwin, c);
        }
        else
        {
            done = true;
        }
    }

    endwin();

    debugwin_destroy(dbgwin);
    cb6502_destroy();

    return 0;
}
