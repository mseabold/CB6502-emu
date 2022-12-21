#include <ncurses.h>
#include <stdlib.h>
#include <string.h>

#include "debugger.h"
#include "debugwin.h"
#include "cpu.h"
#include "log.h"

#define OP_BUF_SIZE 64



WINDOW *linedebugwin;

typedef struct
{
    uint16_t addr;
    char buf[OP_BUF_SIZE];
} line_info_t;

struct debugwin_s
{
    WINDOW *curswin;
    int height;
    int width;
    debug_t debugger;

    line_info_t *line_info;
    unsigned int curhead;

    unsigned int screenline;

    uint16_t curpc;
};

static unsigned int screenline_to_bufline(debugwin_t handle, unsigned int screenline)
{
    log_print(lDEBUG, "sl2bl(): sl: %u, result: %u, head: %u, height: %u\n", screenline, ((screenline + handle->curhead) % handle->height), handle->curhead, handle->height);
    return (screenline + handle->curhead) % handle->height;
}

static void refill_screen(debugwin_t handle, uint16_t pc_start, unsigned int offset, unsigned int count)
{
    int index;
    unsigned int bufindex;

    log_print(lDEBUG, "refill_screen()\n");

    wmove(handle->curswin, offset, 0);
    for(index=offset,bufindex=(handle->curhead + offset)%handle->height; index < offset+count && pc_start <= 0xffff; ++index,bufindex=(bufindex+1)%handle->height)
    {
        cpu_disassemble_at(pc_start, OP_BUF_SIZE, handle->line_info[bufindex].buf);
        handle->line_info[bufindex].addr = pc_start;
        wprintw(handle->curswin, "%s%s", handle->line_info[bufindex].buf, (index == handle->height-1) ? "" : "\n");
        pc_start += cpu_get_op_len_at(pc_start);
    }

    if(linedebugwin)
    {
            wmove(linedebugwin, 0, 0);
        for(index=0;index<handle->height;index++)
        {
            wprintw(linedebugwin, "%s\n", handle->line_info[(handle->curhead + index) % handle->height].buf);
            wrefresh(linedebugwin);
        }
    }
    wrefresh(handle->curswin);
}

static void selectline(debugwin_t handle, unsigned int newline)
{
    log_print(lDEBUG, "selectline: scr: %u, head: %u, bufl:%u, newl: %u, bufval: %s, pc: 0x%04x\n", handle->screenline, handle->curhead, screenline_to_bufline(handle, newline), newline, handle->line_info[screenline_to_bufline(handle, newline)].buf, cpu_get_reg(REG_PC));
    mvwchgat(handle->curswin, handle->screenline,0,-1,0,0,NULL);
    handle->screenline = newline;
    mvwchgat(handle->curswin, newline,0,strlen(handle->line_info[screenline_to_bufline(handle, newline)].buf),A_REVERSE,0,NULL);
    wrefresh(handle->curswin);
}

static void scrollup(debugwin_t handle, int amount)
{
    int new;
    uint16_t pc;

    wscrl(handle->curswin, amount);
    wmove(handle->curswin, handle->height-amount, 0);

    pc = handle->line_info[screenline_to_bufline(handle, handle->height-1)].addr;
    pc += cpu_get_op_len_at(pc);


    handle->curhead = (handle->curhead + amount) % handle->height;
    log_print(lDEBUG, "change head: %u\n", handle->curhead);
    refill_screen(handle, pc, handle->height - amount, amount);
    handle->screenline -= amount;
    log_print(lDEBUG, "scrollup() newhead: %s bufl[0]: %s\n", handle->line_info[handle->curhead].buf, handle->line_info[screenline_to_bufline(handle, 0)].buf);
}

static int findaddrline(debugwin_t handle, uint16_t addr)
{
    int index;
    int sl;

    for(index=0;index<handle->height;index++)
    {
        if(handle->line_info[index].addr == addr)
        {
            /* Convert line info index to screen line. */
            sl = index - (int)handle->curhead;

            if(sl < 0)
                sl += handle->height;

            return sl;
        }
    }

    return -1;
}

static void refresh_state(debugwin_t handle)
{
    unsigned int nextline;

    handle->curpc = cpu_get_reg(REG_PC);

    nextline = findaddrline(handle, handle->curpc);

    if(nextline != -1)
    {
        if(nextline > handle->screenline)
        {
            /* TODO handle scroll */
            selectline(handle, nextline);
            if(handle->screenline + (handle->height/4) >= handle->height)
            {
                log_print(lDEBUG, "jump + scroll\n");
                scrollup(handle, handle->height/4);
                nextline -= handle->height/4;
            }
        }
        else
        {
            /* Jump backwards, but still on screen. */
            selectline(handle, nextline);
        }
    }
    else
    {
        /* Jump outside of screen, so we need to reload. */
        handle->curhead = 0;
        refill_screen(handle, handle->curpc, 0, handle->height);
        selectline(handle, 0);
    }
}

debugwin_t debugwin_create(WINDOW *curswindow, debug_t debugger, int height, int width)
{
    debugwin_t handle;
    int index;
    uint16_t pc;
    char disbuf[128];

    handle = (debugwin_t)malloc(sizeof(struct debugwin_s));

    if(handle == NULL)
    {
        return NULL;
    }

    handle->line_info = malloc(sizeof(line_info_t) * height);

    if(handle->line_info == NULL)
    {
        free(handle);
        return NULL;
    }


    handle->curswin = curswindow;
    handle->debugger = debugger;
    handle->height = height;
    handle->width = width;
    scrollok(handle->curswin, true);

    handle->screenline = 0;

    pc = cpu_get_reg(REG_PC);

    wrefresh(handle->curswin);
    refill_screen(handle, pc, 0, height);
    wrefresh(handle->curswin);

    selectline(handle, 0);

    return handle;
}

void debugwin_destroy(debugwin_t window)
{
    free(window->line_info);
    free(window);
}

void debugwin_processchar(debugwin_t window, char input)
{
    debug_breakpoint_t bphit;
    int nextline;
    switch(input)
    {
        case 'n':
            /* Scroll if we reach the bottom 1/5 of the screen. */
            debug_next(window->debugger, &bphit);
            refresh_state(window);
            break;
        case 's':
            debug_step(window->debugger);
            refresh_state(window);
            break;
        default:
            break;
    }
}
