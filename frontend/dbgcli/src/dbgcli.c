#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dbgcli.h"
#include "debugger.h"
#include "cpu.h"

#define CMD_DELIM " "
#define MAX_PARAMS 10

typedef struct cmd_param_s
{
    bool int_valid;
    long ival;
    char *sval;
} cmd_param_t;

typedef void (*dbg_cmd_handler_t)(uint32_t num_params, cmd_param_t *params);

typedef struct dbg_cmd_s
{
    const char *longcmd;
    char shortcmd;
    dbg_cmd_handler_t handler;
} dbg_cmd_t;

typedef struct dbgcli_context_s
{
    sys_cxt_t system;
    debug_t debugger;
    bool exit;
} dbgcli_context_t;

static dbgcli_context_t cxt;

static void cmd_continue(uint32_t num_params, cmd_param_t *params);
static void cmd_next(uint32_t num_params, cmd_param_t *params);
static void cmd_step(uint32_t num_params, cmd_param_t *params);
static void cmd_breakpoint(uint32_t num_params, cmd_param_t *params);
static void cmd_registers(uint32_t num_params, cmd_param_t *params);
static void cmd_quit(uint32_t num_params, cmd_param_t *params);
static void cmd_examine(uint32_t num_params, cmd_param_t *params);
static void cmd_finish(uint32_t num_params, cmd_param_t *params);

static const dbg_cmd_t dbg_cmd_list[] = {
    { "continue", 'c', cmd_continue },
    { "next", 'n', cmd_next },
    { "step", 's', cmd_step },
    { "registers", 'r', cmd_registers },
    { "breakpoint", 'b', cmd_breakpoint },
    { "quit", 'q', cmd_quit },
    { "examine", 'x', cmd_examine },
    { "finish", 'f', cmd_finish },
};

#define NUM_CMDS (sizeof(dbg_cmd_list)/sizeof(dbg_cmd_t))

static void cmd_continue(uint32_t num_params, cmd_param_t *params)
{
    debug_breakpoint_t bp;

    debug_run(cxt.debugger, &bp);

    if(bp == BREAKPOINT_HANDLE_SW_REQUEST)
        printf("\nSW Break requested\n");
    else
        printf("Breakpoint #%u hit\n", bp);
}

static void cmd_next(uint32_t num_params, cmd_param_t *params)
{
    debug_breakpoint_t bp;

    if(debug_next(cxt.debugger, &bp))
    {
        if(bp == BREAKPOINT_HANDLE_SW_REQUEST)
            printf("\nSW Break requested\n");
        else
            printf("Breakpoint #%u hit\n", bp);
    }
}

static void cmd_finish(uint32_t num_params, cmd_param_t *params)
{
    debug_breakpoint_t bp;

    if(debug_finish(cxt.debugger, &bp))
    {
        if(bp == BREAKPOINT_HANDLE_SW_REQUEST)
            printf("\nSW Break requested\n");
        else
            printf("Breakpoint #%u hit\n", bp);
    }
}

static void cmd_registers(uint32_t num_params, cmd_param_t *params)
{
    cpu_regs_t regs;
    char flags[9];

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

    printf("\tA:  %02x\t\tSP: %02x\n", regs.a, regs.sp);
    printf("\tX:  %02x\t\tY:  %02x\n", regs.x, regs.y);
    printf("\tPC: %04x\tS:  %s\n", regs.pc, flags);
}

static void cmd_breakpoint(uint32_t num_params, cmd_param_t *params)
{
    breakpoint_info_t breakpoints[8];
    uint32_t num_breakpoints = 8;
    uint32_t index;
    debug_breakpoint_t bp;
    bool result;

    if(num_params == 0)
    {
        debug_get_breakpoints(cxt.debugger, &num_breakpoints, breakpoints, NULL);

        for (index = 0; index < num_breakpoints; ++index)
        {
            printf("#%u: 0x%04x\n", breakpoints[index].handle, breakpoints[index].address);
        }
    }
    else
    {
        if(params[0].int_valid)
        {
            result = debug_set_breakpoint_addr(cxt.debugger, &bp, (uint16_t)params[0].ival);
        }
        else
        {
            result = debug_set_breakpoint_label(cxt.debugger, &bp, params[0].sval);
        }

        if(result)
            printf("Breakpoint #%u set\n", bp);
        else
            printf("Unable to set breakpoint\n");
    }
}

static void cmd_quit(uint32_t num_params, cmd_param_t *params)
{
    cxt.exit = true;
}

static void cmd_examine(uint32_t num_params, cmd_param_t *params)
{
    uint16_t len;
    uint16_t i;
    uint16_t addr;
    uint16_t col;

    if(num_params == 0 || !params[0].int_valid || (num_params >= 2 && !params[1].int_valid))
        return;

    if(num_params > 1)
        len = (uint16_t)params[1].ival;
    else
        len = 1;

    addr = (uint16_t)params[0].ival;


    for(i=0,col=0; i<len; ++i)
    {
        if(col == 0)
            printf("%04x:", addr + 16*(i/16));

        ++col;
        printf(" %02x", sys_peek_mem(cxt.system, addr+i));

        if(col == 16)
        {
            printf("\n");
            col = 0;
        }
    }

    if(col != 0)
        printf("\n");

}

static void cmd_step(uint32_t num_params, cmd_param_t *params)
{
    debug_step(cxt.debugger);
}

#if defined(_WIN32) || defined(__CYGWIN__)

//TODO
static void register_ctlc(void)
{
}

static void unregister_ctlc(void)
{
}

#elif defined(__linux__)

#include <signal.h>

static struct sigaction oldact;

static void ctrlc_handler(int signum)
{
    debug_break(cxt.debugger);
}

static void register_ctlc(void)
{
    struct sigaction sigact;

    sigact.sa_handler = ctrlc_handler;
    sigact.sa_flags = 0;
    sigemptyset(&sigact.sa_mask);

    sigaction(SIGINT, &sigact, &oldact);
}

static void unregister_ctlc(void)
{
    sigaction(SIGINT, &oldact, NULL);
}

#else

/* Unknown platform, so just don't do anything. */
static void register_ctlc(void)
{
}

static void unregister_ctlc(void)
{
}

#endif

static const dbg_cmd_t *parse_cmd(char *input, uint32_t *num_params, cmd_param_t *params)
{
    const dbg_cmd_t *ret = NULL;
    const dbg_cmd_t *cmd;
    size_t inputlen = strlen(input);
    int index;
    char *token;
    char *saveptr;
    char *endptr;
    long ival;

    if(inputlen == 0 || num_params == NULL || params == NULL)
        return NULL;

    *num_params = 0;

    token = strtok_r(input, CMD_DELIM, &saveptr);

    if(token == NULL)
        return NULL;

    inputlen = strlen(token);
    for(index = 0; index < NUM_CMDS; ++index)
    {
        cmd = &dbg_cmd_list[index];
        if(inputlen == 1)
        {
            if(token[0] == cmd->shortcmd)
            {
                ret = cmd;
                break;
            }
        }
        else
        {
            if(strcasecmp(token, cmd->longcmd) == 0)
            {
                ret = cmd;
                break;
            }
        }
    }

    if(ret != NULL)
    {
        while(((token = strtok_r(NULL, CMD_DELIM, &saveptr)) != NULL) && (*num_params < MAX_PARAMS))
        {
            params[*num_params].sval = token;

            endptr = NULL;
            ival = strtol(token, &endptr, 0);

            if(endptr != NULL && *endptr == '\0')
            {
                params[*num_params].int_valid = true;
                params[*num_params].ival = ival;
            }
            else
                params[*num_params].int_valid = false;

            (*num_params)++;
        }
    }

    return ret;
}

int dbgcli_run(sys_cxt_t system, dbgcli_config_t *config)
{
    char disbuf[256];
    char inbuf[256];
    char *input;
    uint32_t num_params;
    cmd_param_t params[MAX_PARAMS];
    const dbg_cmd_t *cmd = NULL;

    cxt.debugger = debug_init(system);

    if(cxt.debugger == NULL)
        return 1;

    if(config)
    {
        if(config->valid_flags & DBGCLI_CONFIG_FLAG_LABEL_FILE_VALID)
        {
            if(!debug_load_labels(cxt.debugger, config->label_file))
                return 1;

            printf("labels loaded\n");
        }
    }

    cxt.exit = false;
    cxt.system = system;

    register_ctlc();

    while(!cxt.exit)
    {
        cpu_disassemble(sizeof(disbuf), disbuf);
        printf("%s\n", disbuf);
        printf(">");
        fflush(stdout);
        input = fgets(inbuf, sizeof(inbuf), stdin);

        if(input == NULL)
        {
            break;
        }

        input[strcspn(input, "\n")] = 0;

        if(strlen(input) == 0 && cmd != NULL)
        {
            /* Repeat the last commmand. */
            cmd->handler(num_params, params);
            continue;
        }

        cmd = parse_cmd(input, &num_params, params);

        if(cmd != NULL)
            cmd->handler(num_params, params);
        else
            printf("Unknown command\n");
    }

    unregister_ctlc();

    return 0;
}
