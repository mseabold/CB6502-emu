#include <stdlib.h>

#include "regwin.h"
#include "cpu.h"

struct regwin_s
{
    WINDOW *curswin;
};

typedef struct regwin_s *regwin_cxt_t;

regwin_t regwin_init(WINDOW *curswin, void *params)
{
    regwin_cxt_t handle;

    handle = malloc(sizeof(struct regwin_s));

    if(handle == NULL)
        return NULL;

    handle->curswin = curswin;

    regwin_refresh(handle);

    return handle;
}

void regwin_refresh(regwin_t window)
{
    cpu_regs_t regs;
    char flags[9];
    regwin_cxt_t handle = (regwin_cxt_t)window;

    cpu_get_regs(&regs);

    flags[0] = (regs.s & 0x80) ? 'N' : '-';
    flags[1] = (regs.s & 0x40) ? 'V' : '-';
    flags[2] = '-';
    flags[3] = (regs.s & 0x10) ? 'B' : '-';
    flags[4] = (regs.s & 0x08) ? 'D' : '-';
    flags[5] = (regs.s & 0x04) ? 'I' : '-';
    flags[6] = (regs.s & 0x02) ? 'Z' : '-';
    flags[7] = (regs.s & 0x01) ? 'C' : '-';
    flags[8] = 0;

    wmove(handle->curswin, 0, 0);
    wprintw(handle->curswin, "A:  %02x\t\tSP: %02x\n", regs.a, regs.sp);
    wprintw(handle->curswin, "X:  %02x\t\tY:  %02x\n", regs.x, regs.y);
    wprintw(handle->curswin, "PC: %04x\tS:  %s\n", regs.pc, flags);

    wrefresh(handle->curswin);
}

void regwin_destroy(regwin_t window)
{
    free(window);
}

