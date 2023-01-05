#include <ncurses.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "bpwin.h"
#include "dbginfo.h"
#include "debugger.h"
#include "codewin.h"
#include "cpu.h"
#include "log.h"

#define OP_BUF_SIZE 64
#define FILE_POOL_SIZE 8

/*
 * This window can operate in two modes:
 *      - Disassembly: screen is populated by disassembling memory contents.
 *                     This is used if no debug information is provided or
 *                     debug information for a given address/file cannot be found
 *
 *      - Source: Screen is filled from the actual source file used to build the
 *                executing image. This is derived from debug information when supplied.
 */
typedef enum
{
    DISASSEMBLY,
    SOURCE
} display_mode_t;

/* Structure for tracking information about the current lines being displayed
 * on screen. The buffer will be allocated and mapped 1-to-1 to screen lines.
 * The line string information is unioned for each mode. In disassembly mode,
 * the buffer is used to hold the disassembly text for each line. In source mode,
 * the pointer is used to point to lines in the loaded file data buffer. */
typedef struct
{
    uint16_t addr;
    unsigned int depth;
    uint32_t flags;

    union {
        char buf[OP_BUF_SIZE];
        char *ptr;
    } line;
} line_info_t;

/* Indicates that a line in the screen buffer does not have an associated address. This
 * means the addr field is invalid. */
#define LINE_INFO_FLAG_NO_ADDR  0x00000001

/* Indicates that a line in the screen buffer is a macro call that has been expanded.
 * This means that though it has an address, it should be skipped over for highlighting
 * in favor of the expansion opcode. */
#define LINE_INFO_FLAG_MACRO    0x00000002

typedef struct
{
    unsigned int index;
    unsigned int fileid;
    unsigned int numlines;
    char *data;
    char **lineptrs;
    const cc65_lineinfo *lineinfo;
} fileinfo_t;

typedef struct
{
    uint32_t active_mask;
    fileinfo_t files[FILE_POOL_SIZE];
} file_pool_t;

struct codewin_s
{
    WINDOW *curswin;
    int height;
    int width;
    debug_t debugger;
    bpwin_t bpwin;
    uint16_t curpc;
    line_info_t *line_info;
    bool line_info_valid;
    display_mode_t mode;
    int hiline;

    /* State for dbginfo-based code display. */
    bool dbginfo_valid;
    codewin_dbginfo_t dbginfo;
    cc65_linedata curline;
    unsigned int topline;
    file_pool_t filepool;
};

static const char EMPTY_LINE[] = "~";

/* Forward declared some common functions. */
static int find_addr_on_screen(codewin_t handle, uint16_t addr);
static void refresh_state(codewin_t handle);

/**** Functions related to Disassembly Mode ****/

/* Refills the line info buffer and the screen buffer in disassembly mode
 * starting at the supplied PC. */
static void refill_disassembly(codewin_t handle, uint16_t pc_start)
{
    unsigned int index;

    log_print(lDEBUG, "refill_disassembly()\n");

    wmove(handle->curswin, 0, 0);

    for(index=0; index < handle->height; ++index)
    {
        cpu_disassemble_at(pc_start, OP_BUF_SIZE, handle->line_info[index].line.buf);
        handle->line_info[index].addr = pc_start;
        handle->line_info[index].flags = 0;
        wprintw(handle->curswin, "%s%s", handle->line_info[index].line.buf, (index == handle->height-1) ? "" : "\n");
        pc_start += cpu_get_op_len_at(pc_start);
    }

    wrefresh(handle->curswin);

    handle->line_info_valid = true;
}

/* Changes the highlighted line in disassembly mode. */
static void selectline(codewin_t handle, unsigned int newline)
{
    if(handle->hiline != -1)
    {
        mvwchgat(handle->curswin, handle->hiline,0,-1,0,0,NULL);
    }

    handle->hiline = newline;
    mvwchgat(handle->curswin, newline,0,strlen(handle->line_info[newline].line.buf),A_REVERSE,0,NULL);
    wrefresh(handle->curswin);
}

/* Scroll the current disassembly text up by amount lines. */
static void scrollup(codewin_t handle, int amount)
{
    uint16_t pc;

    log_print(lDEBUG, "scrollup() %d\n", amount);

    pc = handle->line_info[amount].addr;

    refill_disassembly(handle, pc);
    handle->hiline -= amount;
}

/* Refresh the window state in disassembly mode. */
static void refresh_disassembly(codewin_t handle)
{
    unsigned int nextline;

    if(handle->mode == SOURCE)
    {
        handle->line_info_valid = false;
    }

    handle->mode = DISASSEMBLY;

    nextline = find_addr_on_screen(handle, handle->curpc);

    if(nextline != -1)
    {
        if(nextline > handle->hiline)
        {
            /* TODO handle scroll */
            if(nextline + (handle->height/4) >= handle->height)
            {
                log_print(lDEBUG, "jump + scroll\n");
                scrollup(handle, handle->height/4);
                nextline -= handle->height/4;
            }
            selectline(handle, nextline);
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
        refill_disassembly(handle, handle->curpc);
        selectline(handle, 0);
    }
}

/**** Function related to Source Mode ****/

/* Builds a source file name based on the base name in the debug information and
 * the supplied debug options. The options include replacing a substring of the given filename
 * as well as prefixing the base name with an additional path. */
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

/* Free data that has been allocated to track a file. */
static void free_fileinfo(codewin_t handle, fileinfo_t *fileinfo)
{
    if(fileinfo->data != NULL)
    {
        free(fileinfo->data);
    }

    if(fileinfo->lineptrs != NULL)
    {
        free(fileinfo->lineptrs);
    }

    if(fileinfo->lineinfo != NULL)
    {
        cc65_free_lineinfo(handle->dbginfo.handle, fileinfo->lineinfo);
    }

    memset(fileinfo, 0, sizeof(fileinfo_t));
}

/* Find an available entry in the file pool. This will prefer a unused entry
 * in the pool, but will fall back to an entry with allocated filed data that
 * is not currently being displayed on screen. */
static int find_free_fileinfo(codewin_t handle)
{
    unsigned int index;
    unsigned int inactive_index;
    fileinfo_t *inactive = NULL;
    fileinfo_t *freeinfo = NULL;
    file_pool_t *pool = &handle->filepool;

    if(pool->active_mask == (1 << FILE_POOL_SIZE) - 1)
    {
        log_print(lNOTICE, "All file pool entries are active (displayed on current screen\n");
        return -1;
    }

    for(index = 0; index < FILE_POOL_SIZE; ++index)
    {
        if((pool->active_mask & (1 << index)) == 0)
        {
            /* Flag the first inactive entry we find in case we
             * do not find a free entry. */
            if(inactive == NULL)
            {
                inactive_index = index;
                inactive = &pool->files[index];
            }

            if(pool->files[index].data == NULL)
            {
                freeinfo = &pool->files[index];
                break;
            }
        }
    }

    if(inactive == NULL)
    {
        log_print(lERROR, "Unable to find inactive file pool entry despite mask indicating otherwise\n");
        return -1;
    }

    if(freeinfo == NULL)
    {
        /* We could not find a free entry, but we found an inactive one. Free the existing loaded information
         * in the inactive entry. */
        free_fileinfo(handle, inactive);
        index = inactive_index;
    }

    log_print(lDEBUG, "find_free_file_info(): %p, %u\n", freeinfo, (freeinfo == NULL) ? 0 : freeinfo->index);

    return index;
}

/* Find a file info entry in the pool based on file ID. */
static fileinfo_t *find_file_id(file_pool_t *pool, unsigned int fileid)
{
    int index;

    for(index = 0; index < FILE_POOL_SIZE; ++index)
    {
        if(pool->files[index].data != NULL && pool->files[index].fileid == fileid)
        {
            return &pool->files[index];
        }
    }

    return NULL;
}

/* Loads a source file based on the debug info file ID. Information is loaded
 * into the supplied fileinfo_t pointer. This returns true if the file was
 * successfully loaded. */
static bool load_file(codewin_t handle, unsigned int fileid, unsigned int fileindex)
{
    const cc65_sourceinfo *sourceinfo;
    FILE *f;
    char filename[128];
    unsigned long size;
    unsigned int index;
    fileinfo_t *fileinfo = &handle->filepool.files[fileindex];

    log_print(lDEBUG, "load_file(): %u, %u\n", fileid, index);

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

    fileinfo->data = malloc(sourceinfo->data[0].source_size);

    if(fileinfo->data == NULL)
    {
        log_print(lWARNING, "Unable to allocate memory for file\n");
        cc65_free_sourceinfo(handle->dbginfo.handle, sourceinfo);
        return false;
    }

    size = fread(fileinfo->data, 1, sourceinfo->data[0].source_size, f);

    if(size < sourceinfo->data[0].source_size)
    {
        log_print(lERROR, "File read did not return expected amount. Read: %lu, Size: %lu\n", size, sourceinfo->data[0].source_size);
        cc65_free_sourceinfo(handle->dbginfo.handle, sourceinfo);
        fclose(f);
        free_fileinfo(handle, fileinfo);
        return false;
    }

    size = sourceinfo->data[0].source_size;
    fclose(f);

    fileinfo->fileid = sourceinfo->data[0].source_id;
    cc65_free_sourceinfo(handle->dbginfo.handle, sourceinfo);

    /* Count the number of lines in the file. */
    fileinfo->numlines = 0;

    for(index = 0; index < size; ++index)
    {
        if(fileinfo->data[index] == '\n')
        {
            fileinfo->numlines++;
        }
    }

    /* Allocate an array of pointers to point to each line in the file data. */
    fileinfo->lineptrs = malloc(fileinfo->numlines * sizeof(char *));

    if(fileinfo->lineptrs == NULL)
    {
        log_print(lWARNING, "Unable to allocate memory");
        free_fileinfo(handle, fileinfo);
        return false;
    }

    /* Run through the file again. This time, store a pointer to each
     * line in the file, then replace the newline with a null byte
     * to transform each line into an individual string. */
    fileinfo->lineptrs[0] = fileinfo->data;
    fileinfo->numlines = 1;

    for(index = 0; index < size; ++index)
    {
        if(fileinfo->data[index] == '\n')
        {
            if(index < size - 1)
            {
                fileinfo->lineptrs[fileinfo->numlines++] = &fileinfo->data[index+1];
            }

            fileinfo->data[index] = '\0';
        }
    }

    /* Finally, get the debug line information for the file.
     * NOTE: While normally, we could just query this information when necessary
     *       and let the dbginfo module own the memory, the multiple-line-info quirk
     *       described elsewhere means we are not able to actually query *all* of the
     *       line information based on source/line number. We does this exact query when
     *       evaluating macro expansions. Instead, we store our own copy of this information
     *       so that we can search and query on our own terms. */
    fileinfo->lineinfo = cc65_line_bysource(handle->dbginfo.handle, fileinfo->fileid);

    if(fileinfo->lineinfo == NULL)
    {
        free_fileinfo(handle, fileinfo);
        return false;
    }

    /* Store the index we allocated for reference. */
    fileinfo->index = fileindex;

    return true;
}

/* Attempt to found and (optionally) load the specified file. If set_active is
 * true, the fileinfo will be marked as active in the file pool, indicating that
 * is actively being displayed on screen and cannot be culled. */
static fileinfo_t *get_file_info(codewin_t handle, unsigned int fileid, bool load, bool set_active)
{
    fileinfo_t *info = NULL;
    int index;

    info = find_file_id(&handle->filepool, fileid);

    if(info == NULL && load)
    {
        index = find_free_fileinfo(handle);

        if(index == -1)
        {
            /* File pool is full, so we can't load the file. */
            return NULL;
        }

        if(load_file(handle, fileid, index))
        {
            info = &handle->filepool.files[index];
        }
    }

    if(info != NULL && set_active)
    {
        handle->filepool.active_mask |= (1 << info->index);
    }

    return info;
}

/* Recursive binary search function for locating debug line info for a given line
 * number in a source file. */
static int find_line_index_r(const fileinfo_t *fileinfo, unsigned int line, int low, int high)
{
    unsigned int check;
    unsigned int checkline;

    if(high < low)
    {
        return -1;
    }

    check = (low + high) / 2;
    checkline = fileinfo->lineinfo->data[check].source_line;

    if(line < checkline)
    {
        return find_line_index_r(fileinfo, line, low, check-1);
    }
    else if(line > checkline)
    {
        return find_line_index_r(fileinfo, line, check+1, high);
    }
    else
    {
        return check;
    }
}

/* Top level search function for debug line info based on line number in a source.
 * If there are multiple line info entries for the given line number, this function
 * is guaranteed to return the first entry index that matches. */
static int find_line_index(const fileinfo_t *fileinfo, unsigned int line)
{
    int index;

    index = find_line_index_r(fileinfo, line, 0, fileinfo->lineinfo->count-1);

    /* If any previous entries also match, select them instead.
     * NOTE: This is possible due to a quirk within the cc65 debug info generation.
     *       Any macro that is defined in a header will get a uniqie "line" entry in
     *       the debug file for each source file that includes the header. */
    while(index > 0 && fileinfo->lineinfo->data[index-1].source_line == line)
    {
        --index;
    }

    return index;
}

/*
 * Searches for line information tha maps the given address at the given
 * nested macro depth. A depth of 0 indicates top-level macro references,
 * and larger depths indicate nested macro definitions/references.
 *
 * If this function returns true, the supplied line data pointer will contain the
 * found corresponding line information. If this returns false, no line information
 * was found corresponding to the given address and depth.
 */
static bool line_by_addr_depth(codewin_t handle, unsigned int addr, unsigned int depth, cc65_linedata *line)
{
    bool found = false;
    const cc65_spaninfo *spaninfo;
    const cc65_lineinfo *lineinfo;
    unsigned int index;

    spaninfo = cc65_span_byaddr(handle->dbginfo.handle, addr);

    if(spaninfo == NULL)
    {
        return false;
    }

    for(index = 0; index < spaninfo->count; ++index)
    {
        if(spaninfo->data[index].line_count > 0)
        {
            lineinfo = cc65_line_byspan(handle->dbginfo.handle, spaninfo->data[index].span_id);

            if(lineinfo != NULL)
            {
                if(lineinfo->count > 1)
                {
                    /* This should not happen as far as I am aware for ASM sources, since a newline
                     * is meaningful separator. */
                    log_print(lNOTICE, "More than one line for span: %u\n", spaninfo->data[index].span_id);
                }

                if(lineinfo->data[0].count == depth)
                {
                    found = true;
                    *line = lineinfo->data[0];
                    break;
                }

                cc65_free_lineinfo(handle->dbginfo.handle, lineinfo);
            }
        }
    }

    cc65_free_spaninfo(handle->dbginfo.handle, spaninfo);

    return found;
}

/* Attempts to locate a span entry for a given source line based on its associated assembly address. There
 * can be multiple spans associated for the same line, particulary for macros, so this function locates
 * the correct span based on address. */
static bool span_from_line_addr(codewin_t handle, const fileinfo_t *fileinfo, unsigned int line, unsigned int addr, cc65_spandata *span)
{
    const cc65_spaninfo *spaninfo;
    unsigned int index, index2;
    bool found = false;
    int lineindex;

    lineindex = find_line_index(fileinfo, line);

    if(lineindex < 0)
    {
        /* This is relatively normal for lines such as comments or empty lines. */
        log_print(lDEBUG, "Unable to find line information for file %u line %u\n", fileinfo->fileid, line);
        return false;
    }

    /* There may be multiple line info entries for a given source/line combo (see above comment).
     * The info is sorted by line, and find_line_index() gives us the index of the first matching entry.
     * Loop through the line info for every subsequent index that references the same line. */
    for(index = lineindex; index < fileinfo->lineinfo->count && fileinfo->lineinfo->data[index].source_line == line; ++index)
    {
        spaninfo = cc65_span_byline(handle->dbginfo.handle, fileinfo->lineinfo->data[index].line_id);

        if(spaninfo != NULL)
        {
            for(index2 = 0; index2 < spaninfo->count; ++index2)
            {
                if(addr >= spaninfo->data[index2].span_start && addr <= spaninfo->data[index2].span_end)
                {
                    *span = spaninfo->data[index2];
                    found = true;
                    break;
                }
            }

            cc65_free_spaninfo(handle->dbginfo.handle, spaninfo);
        }
        else
        {
            log_print(lINFO, "Unable to find span from line id: %u\n", fileinfo->lineinfo->data[index].line_id);
        }
    }

    return found;
}

/**
 * Recursive function that fills (part of) the screen from the source/line.
 * It recurses for lines that are macro references, and loads from the macro definition
 * source/line until the macro span is filled.
 *
 * It can stop filling from the given file based on certain conditions:
 *  - The screen is full
 *  - There are no more lines in the file
 *  - The span of the last written line reaches the end of the parent macro references span
 *
 * @param[in] handle     Code window handle
 * @param[in,out] line   The current line on the screen. This will be updated by each recursive call.
 * @param[in] source_id  ID of the source fill to fill from
 * @param[in] start_line Line in the source fill to start filling
 * @param[in] depth      The current recursive/macro depth
 * @param[in/out] addr   The current refernce address for associated code image. This will be updated by each recursive call
 * @param [in] parent    If non-NULL, represents the span of the parent line that recursive call should fill.
 */
static void fill_from_file(codewin_t handle, unsigned int *line, unsigned int source_id, unsigned int start_line, unsigned int depth, unsigned int *addr, const cc65_spandata *parent)
{
    fileinfo_t *fileinfo;
    unsigned int sourceline;
    cc65_linedata nested;
    cc65_spandata spandata;
    unsigned int index;
    unsigned int drawline;

    fileinfo = get_file_info(handle, source_id, true, true);

    if(fileinfo == NULL)
    {
        /* Unable to load the file. */
        log_print(lWARNING, "Unable to open/read source id %u\n", source_id);

        /* Switch to disassembly mode if any file fails. This may be overkill
         * in the case of failure to read a macro expansion, but the handling
         * would get much more complicated. */
        refresh_disassembly(handle);

        return;
    }

    sourceline = start_line;

    /* Continue printing lines unless:
     *      - There are no more lines on the screen
     *      - There are no more lines in the current file
     *      - We reached the end of a valid parent span
     *      - An error caused us to switch to dissassembly mode
     */
    while(*line < handle->height && sourceline < fileinfo->numlines
            && (parent == NULL || *addr <= parent->span_end)
            && handle->mode == SOURCE)
    {
        /* Print the current line first. */
        drawline = (*line)++;
        handle->line_info[drawline].line.ptr = fileinfo->lineptrs[sourceline-1];
        handle->line_info[drawline].depth = depth;
        handle->line_info[drawline].flags = 0;

        /* Find the span information for this line. */
        if(span_from_line_addr(handle, fileinfo, sourceline, *addr, &spandata))
        {
            handle->line_info[drawline].addr = spandata.span_start;

            /* Line consumes address space. Check if there are any nested lines. */
            if(line_by_addr_depth(handle, *addr, depth + 1, &nested))
            {
                /* Flag that the line is a macro line so it should be skipped. */
                handle->line_info[drawline].flags |= LINE_INFO_FLAG_MACRO;

                fill_from_file(handle, line, nested.source_id, nested.source_line, depth+1, addr, &spandata);
            }

            *addr = spandata.span_end + 1;
        }
        else
        {
            /* Line has no associated debug information. This is generally
             * common in source files for things such as blank lines or
             * comments. Flag in the screen buffer that the line has no
             * address. */
            handle->line_info[drawline].flags |= LINE_INFO_FLAG_NO_ADDR;
        }


        ++sourceline;
    }
}

/* Refill the screen and screen buffer with source file information using the
 * debug information. */
static bool refill_source(codewin_t handle, int top_padding)
{
    unsigned int maxdepth;
    const cc65_spaninfo *spaninfo;
    const cc65_lineinfo *lineinfo;
    unsigned int line;
    unsigned int index;
    unsigned int index2;
    unsigned int addr;
    unsigned int sourceline;

    /* Reset the active file mask. It will rebuilt as we load line into the screen buffer. */
    handle->filepool.active_mask = 0;

    line = 0;
    addr = handle->curpc;

    if(handle->curline.source_line > top_padding)
    {
        sourceline = handle->curline.source_line - top_padding;
    }
    else
    {
        sourceline = 0;
    }

    /* Start filling the screen from the top-level source line determined and saved
     * in curline. Starting depth is 0 and there is no parent. */
    fill_from_file(handle, &line, handle->curline.source_id, sourceline, 0, &addr, NULL);

    if(handle->mode == DISASSEMBLY)
    {
        /* An error occurred and we switched modes inside fill_from_file. */
        return false;
    }

    /* If fill_from_file() was unable to fill the whole screen (likely
     * due to there not being enough data in the source file), line
     * will point to the next unfilled line. Starting from there,
     * put a string to indicate an empty line (as opposed to whitespace
     * actually being present in the file). */
    for(index = line; index < handle->height; ++index)
    {
        handle->line_info[index].depth = 0;
        handle->line_info[index].line.ptr = (char *)EMPTY_LINE;
    }

    wclear(handle->curswin);
    for(index = 0; index < handle->height; ++index)
    {
        wmove(handle->curswin, index, 0);

        /* Tab over macro expansions underneath the original macro reference. */
        for(index2 = 0; index2 < handle->line_info[index].depth; ++index2)
        {
            waddstr(handle->curswin, "    ");
        }

        waddstr(handle->curswin, handle->line_info[index].line.ptr);
    }

    handle->line_info_valid = true;

    return true;
}

/* Refresh the state of the window in Source Mode. */
static void refresh_source(codewin_t handle)
{
    const cc65_sourceinfo *sourceinfo;
    unsigned int topline;
    bool redraw = true;
    int linelen;
    char *lineptr;
    int padding;
    int c;

    int addrline;
    unsigned int index;

    if(handle->mode == DISASSEMBLY)
    {
        /* Make sure we re-load the screen if we are switching modes. */
        handle->line_info_valid = false;
    }

    handle->mode = SOURCE;

    addrline = find_addr_on_screen(handle, handle->curpc);

    if(addrline >= 0)
    {
        /* New line is on screen. If it is near the bottom, we should scroll down. */
        if(addrline <= handle->height - 3)
        {
            redraw = false;
        }
    }

    if(redraw)
    {
        padding = 5;
        handle->hiline = -1;

        /* Try to redraw the screen with less and less padding
         * if the padding pushes the active line off the screen
         * (potentially due to a large macro expansion in the padding)
         */
        do
        {
            if(!refill_source(handle, padding))
            {
                /* Loading from source failed (file i/o or pool filled).
                 * We have switched modes to dissassembly so bail.
                 */
                return;
            }
            --padding;
            addrline = find_addr_on_screen(handle, handle->curpc);
        } while(padding >= 0 && addrline == -1);

        /* We should never get to this point, because at worst the
         * current line will slide to the very top of the screen. */
        assert(addrline >= 0);
    }

    if(handle->hiline != -1)
    {
        mvwchgat(handle->curswin, handle->hiline, 0, -1, 0, 0, NULL);
    }

    /* Play a little magic to only highlight the non-whitespace text. */
    lineptr = handle->line_info[addrline].line.ptr;

    while(*lineptr == ' ' || *lineptr == '\t')
    {
        ++lineptr;
    }

    index = 0;
    c = mvwinch(handle->curswin, addrline, 0);

    while(c == ' ')
    {
        ++index;
        c = mvwinch(handle->curswin, addrline, index);
    }

    /* Highlight the active line. */
    mvwchgat(handle->curswin, addrline, index, strlen(lineptr), A_REVERSE, 0, NULL);
    handle->hiline = addrline;
    wrefresh(handle->curswin);
}

/**** Functions common to both modes ****/

/* Search the screen buffer for a line that maps to the provided address that
 * can be highlighted/executed. This excludes lines that have no address
 * (comments, blank, etc) and macro lines that have been expanded to their
 * individual opcodes. */
static int find_addr_on_screen(codewin_t handle, uint16_t addr)
{
    int index;

    if(!handle->line_info_valid)
        return -1;

    for(index=0;index<handle->height;index++)
    {
        if(handle->line_info[index].addr == addr)
        {
            if((handle->line_info[index].flags & (LINE_INFO_FLAG_NO_ADDR | LINE_INFO_FLAG_MACRO)) == 0)
            {
                log_print(lDEBUG, "find_addr_on_screen() Found %d\n", index);
                return index;
            }
        }
    }

    log_print(lDEBUG, "find_addr_on_screen() not found\n");
    return -1;
}

/* General top-level refresh. It attempts to refresh into Source Mode if debug info
 * is available. Itfalls back to Disassembly Mode if there is not debug info, the
 * current PC cannot by mapped to a known source, or an error occurred while attempting
 * to load and display source information. */
static void refresh_state(codewin_t handle)
{
    handle->curpc = cpu_get_reg(REG_PC);

    if(!handle->dbginfo_valid)
    {
        refresh_disassembly(handle);
        return;
    }

    if(line_by_addr_depth(handle, handle->curpc, 0, &handle->curline))
    {
        refresh_source(handle);
    }
    else
    {
        refresh_disassembly(handle);
    }
}

/**** API Functions ****/
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

    memset(handle, 0, sizeof(struct codewin_s));

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
    handle->hiline = -1;

    for(index = 0; index < FILE_POOL_SIZE; ++index)
    {
        handle->filepool.files[index].index = index;
    }

    if(num_dbginfo > 0 && dbginfo != NULL)
    {
        handle->dbginfo_valid = true;
        handle->dbginfo = *dbginfo;
    }

    pc = cpu_get_reg(REG_PC);

    /* Load the initial disassembly but don't display it. */
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
            log_print(lDEBUG, "finish. New PC: %04x\n", cpu_get_reg(REG_PC));
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
