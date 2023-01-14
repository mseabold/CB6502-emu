#ifndef __LOGWIN_H__
#define __LOGWIN_H__

#include "curs_common.h"
#include "log.h"

void *logwin_init(WINDOW *win, void *params);
void logwin_print(log_level_t level, const char *logstr);

#endif /* end of include guard: __LOGWIN_H__ */
