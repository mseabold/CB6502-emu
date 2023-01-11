#include <stdio.h>
#include <argp.h>
#include <stdlib.h>

#include "cb6502.h"
#include "dbginfo.h"
#include "debugger.h"
#include "log.h"
#include "syslog_log.h"
#include "cursmgr.h"
#include "acia.h"
#include "memwin.h"

const char *argp_program_version = "cbconsole 0.1";

typedef struct
{
    char *dbgfile;
    char *acia_sock;
    char *romfile;
    char *source_prefix;
    char *labelfile;
    log_level_t log_level;
} arguments_t;

#define KEY_LABELS 0x100

static struct argp_option options[] =
{
    { "dbgfile", 'd', "DBGFILE", 0, "cc65 debug information file for ROM image" },
    { "aciasock", 's', "SOCK", 0, "socket file for ACIA" },
    { "dbgprefix", 'p', "PREFIX", 0, "source prefix to append to source files in the debug information" },
    { "loglevel", 'l', "LEVEL", 0, "log level, 0=None ... 5=Error" },
    { "labels", KEY_LABELS, "LABELFILE", 0, "VICES label file export from cc65" },
    { 0 }
};

static char doc[] = "CB6502 emulator ncurses console application";
static char arg_doc[] = "ROM_FILE";

static error_t parse_opt(int key, char *arg, struct argp_state *state)
{
    arguments_t *args = (arguments_t *)state->input;
    char *endptr;
    long val;

    switch(key)
    {
        case 'd':
            args->dbgfile = arg;
            break;
        case 's':
            args->dbgfile = arg;
            break;
        case 'p':
            args->source_prefix = arg;
            break;
        case 'l':
            val = strtol(arg, &endptr, 10);

            if(endptr == NULL || *endptr != 0 || val < 0 || val > 5)
            {
                fprintf(stderr, "Invalid log level\n");
                argp_usage(state);
            }

            args->log_level = (log_level_t)val;
            break;
        case KEY_LABELS:
            args->labelfile = arg;
            break;
        case ARGP_KEY_ARG:
            if(state->arg_num >= 1)
            {
                argp_usage(state);
            }

            args->romfile = arg;
            break;
        case ARGP_KEY_END:
            if(state->arg_num < 1)
                argp_usage(state);
            break;
        default:
            return ARGP_ERR_UNKNOWN;
    }

    return 0;
}

static struct argp argp = { options, parse_opt, arg_doc, doc };

static cursmgr_codewin_params_t bplink =
{
    1,
};

static codewin_params_t codewin_params =
{
    NULL,
    0,
    NULL
};

static bpwin_params_t bpwin_params =
{
    NULL
};

static memwin_params_t memwin_params =
{
    NULL
};


static window_info_t windows[] =
{
    { CODE,       0, 0, 0, 0, NULL, 0, &bplink, &codewin_params },
    { BREAKPOINT, 0, 0, 0, 0, NULL, 0, NULL,    &bpwin_params   },
    { REGISTERS,  0, 0, 0, 0, NULL, 0, NULL,    NULL            },
    { MEMORY,     0, 0, 0, 0, NULL, 0, NULL,    &memwin_params  },
};

void dbginfo_error(const cc65_parseerror *error)
{
    fprintf(stderr, "Dbg Parse Error: %c: %s %u:%u %s\n", error->type == CC65_ERROR ? 'E' : 'W', error->name, error->line, error->column, error->errormsg);
}

int main(int argc, char *argv[])
{
    arguments_t args;
    codewin_dbginfo_t dbginfo;
    unsigned int height, width;
    unsigned int halfwidth;
    debug_t debugger;
    sys_cxt_t sys;

    args.romfile = NULL;
    args.acia_sock = ACIA_DEFAULT_SOCKNAME;
    args.dbgfile = NULL;
    args.source_prefix = NULL;
    args.labelfile = NULL;
    args.log_level = lNONE;

    if(argp_parse(&argp, argc, argv, 0, 0, &args) != 0)
        return 1;

    log_set_handler(syslog_log_print);
    log_set_level(args.log_level);

    if(args.dbgfile != NULL)
    {
        /* Load the debug information. */
        dbginfo.handle = cc65_read_dbginfo(args.dbgfile, dbginfo_error);
        dbginfo.valid_options = 0;

        if(dbginfo.handle == NULL)
        {
            return 1;
        }

        if(args.source_prefix != NULL)
        {
            /* Add in the source prefix is given. */
            dbginfo.source_prefix = args.source_prefix;
            dbginfo.valid_options |= DBGOPT_SOURCE_PREFIX;
        }

        codewin_params.num_dbginfo = 1;
        codewin_params.dbginfo = &dbginfo;
    }
    else
        dbginfo.handle = NULL;

    /* Initialize the core CB6502 platform. */
    if(!cb6502_init(args.romfile, args.acia_sock))
    {
        fprintf(stderr, "Unable to initialize the platform\n");

        if(dbginfo.handle != NULL)
            cc65_free_dbginfo(dbginfo.handle);

        return 1;
    }

    /* Create some emulator modules used by this application. */
    sys = cb6502_get_sys();
    debugger = debug_init(sys);

    if(debugger == NULL)
    {
        fprintf(stderr, "Unable to create debugger\n");

        if(dbginfo.handle != NULL)
            cc65_free_dbginfo(dbginfo.handle);

        cb6502_destroy();

        return 1;
    }

    if(args.labelfile)
    {
        debug_load_labels(debugger, args.labelfile);
    }

    if(args.dbgfile)
    {
        debug_set_dbginfo(debugger, 1, &dbginfo.handle);
    }

    /* Start the curses manager, getting the height and width of the screen. */
    cursmgr_init(&height, &width);

    /* Make sure there is a resonable amount of space to show everything we need. */
    if(height > 15)
    {
        /* Set up the window locations based on the screen size. */

        /* First divide the screen in half (rounded up). */
        halfwidth = (width + 1) / 2;

        /* Code window is left half and top of screen, Leaving 7 lines on the bottom for
         * breakpoints. */
        windows[0].width = halfwidth;
        windows[0].height = height - 7;
        codewin_params.debugger = debugger;

        /* Breakpoint window is below code window. Note the border padding is intentionally
         * overlapped. */
        windows[1].y = windows[0].height - 1;
        windows[1].height = 8;
        windows[1].width = halfwidth;
        bpwin_params.debugger = debugger;

        /* Registers window is top-right, again overlapping the border padding on the left. */
        windows[2].x = halfwidth - 1;
        windows[2].width = width - halfwidth + 1;
        windows[2].height = 5;

        /* Memory watch window is below registers. */
        windows[3].y = 4;
        windows[3].x = halfwidth - 1;
        windows[3].width = width - halfwidth + 1;
        windows[3].height = height - 4;
        memwin_params.sys = sys;

        /* Run the main curses manager loop. */
        cursmgr_run(4, windows);
    }
    else
        fprintf(stderr, "Screen is too small to display the cbconsole manager.");

    /* Cleanup the curses manager and restore the original terminal settings */
    cursmgr_cleanup();

    if(dbginfo.handle != NULL)
    {
        cc65_free_dbginfo(dbginfo.handle);
    }

    cb6502_destroy();
}
