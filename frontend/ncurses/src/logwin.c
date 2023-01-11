#include "logwin.h"

static WINDOW *logwindow = NULL;

void *logwin_init(WINDOW *win, void *params)
{
    logwindow = win;
    scrollok(win, true);

    /* Just return something non-null. We won't use it later. */
    return (void *)1;
}

void logwin_print(log_level_t level, const char *logstr)
{
    if(logwindow == NULL)
        return;

    wprintw(logwindow, "%s", logstr);
    wrefresh(logwindow);
}
