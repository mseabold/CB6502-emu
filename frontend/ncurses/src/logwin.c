#include "logwin.h"

static WINDOW *logwindow = NULL;

void curses_logwin_init(WINDOW *win)
{
    logwindow = win;
    scrollok(win, true);
}

void curses_logwin_print(log_level_t level, const char *logstr)
{
    if(logwindow == NULL)
        return;

    wprintw(logwindow, "%s", logstr);
    wrefresh(logwindow);
}
