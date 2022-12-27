#ifndef __REGWIN_H__
#define __REGWIN_H__

#include <curses.h>

typedef struct regwin_s *regwin_t;

regwin_t regwin_init(WINDOW *curswin);
void regwin_refresh(regwin_t window);
void regwin_destroy(regwin_t window);

#endif /* end of include guard: __REGWIN_H__ */
