#include <ncurses.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "bpwin.h"
#include "debugger.h"
#include "codewin.h"
#include "cpu.h"
#include "log.h"

#define OP_BUF_SIZE 64



WINDOW *linedebugwin;

typedef struct
{
    uint16_t addr;
    char buf[OP_BUF_SIZE];
} line_info_t;

struct codewin_s
{
    WINDOW *curswin;
    int height;
    int width;
    debug_t debugger;
    bpwin_t bpwin;
    uint16_t curpc;

    /* State for disassembly-based code display. */
    line_info_t *line_info;
    unsigned int curhead;

    unsigned int screenline;

    /* State for dbginfo-based code display. */
    char *curfiledata;
    unsigned int curfile;
    unsigned int numlines;
    char **lineptrs;

    bool dbginfo_valid;
    codewin_dbginfo_t dbginfo;
    const cc65_spaninfo *curspaninfo;
    const cc65_spandata *curspan;
    cc65_linedata curline;
    unsigned int topline;
    int hiline;

};

static unsigned int screenline_to_bufline(codewin_t handle, unsigned int screenline)
{
    log_print(lDEBUG, "sl2bl(): sl: %u, result: %u, head: %u, height: %u\n", screenline, ((screenline + handle->curhead) % handle->height), handle->curhead, handle->height);
    return (screenline + handle->curhead) % handle->height;
}

static void refill_screen(codewin_t handle, uint16_t pc_start, unsigned int offset, unsigned int count, bool display)
{
    int index;
    unsigned int bufindex;

    log_print(lDEBUG, "refill_screen()\n");

    if(display)
        wmove(handle->curswin, offset, 0);

    for(index=offset,bufindex=(handle->curhead + offset)%handle->height; index < offset+count && pc_start <= 0xffff; ++index,bufindex=(bufindex+1)%handle->height)
    {
        cpu_disassemble_at(pc_start, OP_BUF_SIZE, handle->line_info[bufindex].buf);
        handle->line_info[bufindex].addr = pc_start;
        if(display)
            wprintw(handle->curswin, "%s%s", handle->line_info[bufindex].buf, (index == handle->height-1) ? "" : "\n");
        pc_start += cpu_get_op_len_at(pc_start);
    }

#if 0
    if(linedebugwin)
    {
            wmove(linedebugwin, 0, 0);
        for(index=0;index<handle->height;index++)
        {
            wprintw(linedebugwin, "%s\n", handle->line_info[(handle->curhead + index) % handle->height].buf);
            wrefresh(linedebugwin);
        }
    }
#endif

    if(display)
        wrefresh(handle->curswin);
}

static void selectline(codewin_t handle, unsigned int newline)
{
    log_print(lDEBUG, "selectline: scr: %u, head: %u, bufl:%u, newl: %u, bufval: %s, pc: 0x%04x\n", handle->screenline, handle->curhead, screenline_to_bufline(handle, newline), newline, handle->line_info[screenline_to_bufline(handle, newline)].buf, cpu_get_reg(REG_PC));
    mvwchgat(handle->curswin, handle->screenline,0,-1,0,0,NULL);
    handle->screenline = newline;
    mvwchgat(handle->curswin, newline,0,strlen(handle->line_info[screenline_to_bufline(handle, newline)].buf),A_REVERSE,0,NULL);
    wrefresh(handle->curswin);
}

static void scrollup(codewin_t handle, int amount)
{
    int new;
    uint16_t pc;

    log_print(lDEBUG, "scrollup() %d\n", amount);

    wmove(handle->curswin, handle->height-amount, 0);

    pc = handle->line_info[handle->curhead+amount].addr;


    handle->curhead = (handle->curhead + amount) % handle->height;
    log_print(lDEBUG, "change head: %u\n", handle->curhead);
    refill_screen(handle, pc, 0, handle->height, true);
    handle->screenline -= amount;
    log_print(lDEBUG, "scrollup() newhead: %s bufl[0]: %s\n", handle->line_info[handle->curhead].buf, handle->line_info[screenline_to_bufline(handle, 0)].buf);
}

static int findaddrline(codewin_t handle, uint16_t addr)
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

static void build_filename(codewin_t handle, char *buffer, size_t buffer_size, const char *base_name)
{
    const char *baseptr = base_name;
    char *repl_ptr;
    const char *prefix = "";
    const char *sep = "";
    size_t prefix_len;

    if(handle->dbginfo.valid_options & DBGOPT_SOURCE_REPLACE)
    {
        repl_ptr = strstr(base_name, handle->dbginfo.source_replace);

        if(repl_ptr != NULL && repl_ptr == base_name)
        {
            /* Only perform the replacement if the subtring is at the beginning of the path. */
            baseptr = &base_name[strlen(handle->dbginfo.source_replace)];
            if(baseptr[0] == '/')
                ++baseptr;
        }
    }

    if(handle->dbginfo.valid_options & DBGOPT_SOURCE_PREFIX)
    {
        prefix = handle->dbginfo.source_prefix;
        prefix_len = strlen(prefix);

        if(prefix_len > 0 && prefix[prefix_len-1] != '/')
            sep = "/";
    }

    snprintf(buffer, buffer_size, "%s%s%s", prefix, sep, baseptr);

    log_print(lINFO, "Constructed filename: %s\n", buffer);
}

static bool load_file(codewin_t handle, unsigned int fileid)
{
    const cc65_sourceinfo *sourceinfo;
    FILE *f;
    char filename[128];
    unsigned long size;
    unsigned long index;

    if(handle->curfiledata != NULL)
    {
        free(handle->curfiledata);
        handle->curfiledata = NULL;

        if(handle->lineptrs != NULL)
        {
            free(handle->lineptrs);
            handle->lineptrs = NULL;
        }
    }

    sourceinfo = cc65_source_byid(handle->dbginfo.handle, fileid);

    if(sourceinfo == NULL)
    {
        return false;
    }

    /* Assume one source per ID for our purpose. */
    build_filename(handle, filename, sizeof(filename), sourceinfo->data[0].source_name);

    f = fopen(filename, "r");

    if(f == NULL)
    {
        log_print(lINFO, "Unable to open: %s\n", filename);
        cc65_free_sourceinfo(handle->dbginfo.handle, sourceinfo);
        return false;
    }

    handle->curfiledata = malloc(sourceinfo->data[0].source_size);

    if(handle->curfiledata == NULL)
    {
        log_print(lWARNING, "Unable to allocate memory for file\n");
        cc65_free_sourceinfo(handle->dbginfo.handle, sourceinfo);
        return false;
    }

    fread(handle->curfiledata, 1, sourceinfo->data[0].source_size, f);
    size = sourceinfo->data[0].source_size;
    fclose(f);

    handle->curfile = sourceinfo->data[0].source_id;
    cc65_free_sourceinfo(handle->dbginfo.handle, sourceinfo);

    handle->numlines = 0;

    for(index = 0; index < size; ++index)
    {
        if(handle->curfiledata[index] == '\n')
        {
            handle->numlines++;
        }
    }

    handle->lineptrs = malloc(handle->numlines * sizeof(char *));

    assert(handle->lineptrs != NULL);

    handle->lineptrs[0] = handle->curfiledata;
    handle->numlines = 1;

    for(index = 0; index < size; ++index)
    {
        if(handle->curfiledata[index] == '\n')
        {
            if(index < size - 1)
            {
                handle->lineptrs[handle->numlines++] = &handle->curfiledata[index+1];
            }
        }
    }

    {
        unsigned int tmpline = 69;
        *(handle->lineptrs[tmpline+1] - 1) = 0;
        log_print(lDEBUG, "Random line selection: %u: %s\n", tmpline, handle->lineptrs[tmpline]);
        *(handle->lineptrs[tmpline+1] - 1) = '\n';
    }


    return true;
}

static void refresh_disassembly(codewin_t handle)
{
    unsigned int nextline;

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
                selectline(handle, handle->screenline);
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
        refill_screen(handle, handle->curpc, 0, handle->height, true);
        selectline(handle, 0);
    }
}

static bool find_sourceline(codewin_t handle)
{
    unsigned int index;
    const cc65_lineinfo *lineinfo = NULL;
    const cc65_linedata *linedata = NULL;

    if(handle->curspan != NULL)
    {
        cc65_free_spaninfo(handle->dbginfo.handle, handle->curspaninfo);
        handle->curspaninfo = NULL;
        handle->curspan = NULL;
    }

    handle->curspaninfo = cc65_span_byaddr(handle->dbginfo.handle, handle->curpc);

    if(handle->curspaninfo == NULL)
    {
        log_print(lNOTICE, "Could not find span for 0x%04x\n", handle->curpc);
        goto error;
    }

    handle->curspan = NULL;

    for(index = 0; index < handle->curspaninfo->count; ++index)
    {
        const cc65_spandata *tmp;
        tmp = &handle->curspaninfo->data[index];
        log_print(lDEBUG, "Check span: id %u, start %u, end %u\n", tmp->span_id, tmp->span_start, tmp->span_end);
        /* For now, pick the span that reachest the farthest. This is kind of a hack to
         * generally pack a top level macro line rather than individual nested lines.
         */
        if(handle->curspaninfo->data[index].line_count > 0 && (handle->curspan == NULL || handle->curspaninfo->data[index].span_end > handle->curspan->span_end))
        {
            handle->curspan = &handle->curspaninfo->data[index];
        }
    }

    if(handle->curspan == NULL)
    {
        log_print(lNOTICE, "Span did not contain any source line references\n");
        goto error;
    }

    log_print(lDEBUG, "Select span %u\n", handle->curspan->span_id);

    lineinfo = cc65_line_byspan(handle->dbginfo.handle, handle->curspan->span_id);

    if(lineinfo == NULL)
    {
        log_print(lNOTICE, "Unable to find line information for span %u\n", handle->curspan->span_id);
        goto error;
    }

    for(index = 0; index < lineinfo->count; ++index)
    {
        log_print(lDEBUG, "check line %u: count: %u\n", lineinfo->data[index].line_id, lineinfo->data[index].count);
        /* For now, only take the top-level line if there are multiple. */
        if(lineinfo->data[index].count == 0)
        {
            linedata = &lineinfo->data[index];
            break;
        }
    }

    if(linedata == NULL)
    {
        log_print(lNOTICE, "No top level line found for line\n");
        goto error;
    }

    handle->curline = *linedata;
    log_print(lDEBUG, "Picked line %u from span %u\n", handle->curline.line_id, handle->curspan->span_id);

    return true;

error:
    cc65_free_spaninfo(handle->dbginfo.handle, handle->curspaninfo);
    handle->curspaninfo = NULL;
    handle->curspan = NULL;

    if(lineinfo == NULL)
        cc65_free_lineinfo(handle->dbginfo.handle, lineinfo);

    return false;
}

static void refresh_source(codewin_t handle)
{
    const cc65_sourceinfo *sourceinfo;
    unsigned int topline;
    bool redraw = true;
    int linelen;
    char *lineptr;

    if(handle->curfiledata == NULL || handle->curfile != handle->curline.source_id)
    {
        log_print(lDEBUG, "New File: data: %p, cur: %u, new: %u\n", handle->curfiledata, handle->curfile, handle->curline.source_id);
        if(!load_file(handle, handle->curline.source_id))
        {
            log_print(lWARNING, "Unable to load file ID: %u\n", handle->curline.source_id);
            refresh_disassembly(handle);
            return;
        }
        else
        {
            log_print(lDEBUG, "File loaded: %p\n", handle->curfiledata);
        }

        /* TODO configurable top padding? */
        if(handle->curline.source_line >= 5)
        {
            handle->topline = handle->curline.source_line - 5;
        }
        else
        {
            handle->topline = 0;
        }
    }
    else
    {
        if(handle->curline.source_line < handle->topline || handle->curline.source_line >= handle->topline+handle->height)
        {
            /* New line is off screen, so just load from the new line with padding. */
            if(handle->curline.source_line >= 5)
            {
                handle->topline = handle->curline.source_line - 5;
            }
            else
            {
                handle->topline = 0;
            }
        }
        else if(handle->curline.source_line - handle->topline > handle->height - 3)
        {
            /* New line is on screen, but near the end, so scroll up some. */
            handle->topline += handle->height*4/5;
        }
        else
        {
            /* New line is on screen and we don't need to change the screen. */
            redraw = false;
        }
    }

    log_print(lDEBUG, "refresh_source(): topline: %u, curline: %u\n", handle->topline, handle->curline.source_line);

    if(redraw)
    {
        wclear(handle->curswin);
        wmove(handle->curswin, 0, 0);
        wprintw(handle->curswin, "%s", handle->lineptrs[handle->topline-1]);
    }

    if(handle->hiline != -1)
    {
        mvwchgat(handle->curswin, handle->hiline, 0, -1, 0, 0, NULL);
    }

    handle->hiline = handle->curline.source_line - handle->topline;
    lineptr = handle->lineptrs[handle->curline.source_line - 1];
    linelen = strchr(lineptr, '\n') - lineptr;
    mvwchgat(handle->curswin, handle->hiline, 0, linelen, A_REVERSE, 0, NULL);
    wrefresh(handle->curswin);
}

static void refresh_state(codewin_t handle)
{
    handle->curpc = cpu_get_reg(REG_PC);

    if(!handle->dbginfo_valid)
    {
        refresh_disassembly(handle);
        return;
    }

    if(handle->curspan != NULL && handle->curpc >= handle->curspan->span_start && handle->curpc <= handle->curspan->span_end)
    {
        /* Nothing to do as we remain in the current span. */
        log_print(lDEBUG, "Same span PC: %04x, start: %04x, end: %04x\n", handle->curpc, handle->curspan->span_start, handle->curspan->span_end);
        return;
    }

    if(find_sourceline(handle))
    {
        refresh_source(handle);
    }
    else
    {
        refresh_disassembly(handle);
    }
}

codewin_t codewin_create(WINDOW *curswindow, debug_t debugger, unsigned int num_dbginfo, const codewin_dbginfo_t *dbginfo)
{
    codewin_t handle;
    int index;
    uint16_t pc;
    char disbuf[128];

    /* For now, we only support a single dbginfo instance, with future support for
     * multiple dbg files (i.e. ROM and RAM apps). */
    if(num_dbginfo > 1)
    {
        log_print(lNOTICE, "Multiple debug files currently not supported\n");
        return NULL;
    }

    int height,width;

    getmaxyx(curswindow, height, width);

    handle = (codewin_t)malloc(sizeof(struct codewin_s));

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


    handle->curhead = 0;
    handle->curswin = curswindow;
    handle->debugger = debugger;
    handle->height = height;
    handle->width = width;
    handle->hiline = -1;

    if(num_dbginfo > 0 && dbginfo != NULL)
    {
        handle->dbginfo_valid = true;
        handle->dbginfo = *dbginfo;
    }

    handle->screenline = 0;

    pc = cpu_get_reg(REG_PC);

    /* Load the initial disassembly but don't display it. */
    refill_screen(handle, pc, 0, handle->height, true);
    refresh_state(handle);

    return handle;
}

void codewin_destroy(codewin_t window)
{
    free(window->line_info);
    free(window);
}

void codewin_processchar(codewin_t window, char input)
{
    debug_breakpoint_t bphit;
    int nextline;

    if(window->bpwin)
        bpwin_clear_active_bp(window->bpwin);

    switch(input)
    {
        case 'n':
            /* Scroll if we reach the bottom 1/5 of the screen. */
            if(debug_next(window->debugger, &bphit))
            {
                if(window->bpwin != NULL && bphit != BREAKPOINT_HANDLE_SW_REQUEST)
                {
                    bpwin_set_active_bp(window->bpwin, bphit);
                }
            }
            refresh_state(window);
            break;
        case 's':
            debug_step(window->debugger);
            refresh_state(window);
            break;
        case 'f':
            if(debug_finish(window->debugger, &bphit))
            {
                if(window->bpwin != NULL && bphit != BREAKPOINT_HANDLE_SW_REQUEST)
                {
                    bpwin_set_active_bp(window->bpwin, bphit);
                }
            }
            refresh_state(window);
            break;
        case 'c':
            debug_run(window->debugger, &bphit);

            if(window->bpwin != NULL && bphit != BREAKPOINT_HANDLE_SW_REQUEST)
            {
                bpwin_set_active_bp(window->bpwin, bphit);
            }

            refresh_state(window);
            break;
        default:
            break;
    }
}

void codewin_set_bpwin(codewin_t window, bpwin_t breakpoint_window)
{
    window->bpwin = breakpoint_window;
}
