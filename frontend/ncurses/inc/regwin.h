#ifndef __REGWIN_H__
#define __REGWIN_H__

#include "curs_common.h"

typedef void *regwin_t;

void *regwin_init(WINDOW *curswin, void *params);
void regwin_refresh(regwin_t window);
void regwin_resize(regwin_t window);
void regwin_destroy(regwin_t window);

#endif /* end of include guard: __REGWIN_H__ */
