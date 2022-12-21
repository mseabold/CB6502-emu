#ifndef __LOGWIN_H__
#define __LOGWIN_H__

#include <ncurses.h>
#include "log.h"

void curses_logwin_init(WINDOW *win);
void curses_logwin_print(log_level_t level, const char *logstr);

#endif /* end of include guard: __LOGWIN_H__ */
