#include <stdio.h>
#include <ncurses.h>

#include "cpu.h"
#include "acia.h"
#include "cb6502.h"

int main(int argc, char *argv[])
{
    int i;
    char disbuf[128];
    uint16_t pc;

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

    pc =  cpu_get_reg(REG_PC);

    for(i=0;i<LINES;++i)
    {
        cpu_disassemble_at(pc, sizeof(disbuf), disbuf);
        printw("%s\n", disbuf);
        pc += cpu_get_op_len_at(pc);
    }

    mvchgat(3,0,-1,A_REVERSE,0,NULL);

    refresh();
    getch();
    mvchgat(3,0,-1,0,0,NULL);
    mvchgat(4,0,-1,A_REVERSE,0,NULL);
    getch();
    endwin();

    cb6502_destroy();

    return 0;
}
