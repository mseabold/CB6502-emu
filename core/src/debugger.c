#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* TODO portability from linux */
#include <strings.h>

#include "debugger.h"
#include "cpu.h"

#define MAX_BREAKPOINTS 8
#define BREAKPOINT_VALID_MASK 0x80000000
#define BREAKPOINT_ADDR_MASK  0x0000ffff

#define CMD_DELIM " "
#define MAX_PARAMS 16

typedef struct dbg_cxt_s
{
    bool broken;
    uint32_t breakpoints[MAX_BREAKPOINTS];
} dbg_cxt_t;

typedef struct cmd_param_s
{
    bool int_valid;
    long ival;
    char *sval;
} cmd_param_t;

typedef void (*dbg_cmd_handler_t)(dbg_cxt_t *cxt, uint32_t num_params, cmd_param_t *params);

typedef struct dbg_cmd_s
{
    const char *longcmd;
    char shortcmd;
    dbg_cmd_handler_t handler;
} dbg_cmd_t;

static dbg_cxt_t cxt;

static void cmd_continue(dbg_cxt_t *cxt, uint32_t num_params, cmd_param_t *params)
{
    cxt->broken = false;
}

static void cmd_next(dbg_cxt_t *cxt, uint32_t num_params, cmd_param_t *params)
{
    uint16_t pc;

    if(cpu_is_subroutine())
    {
        pc = cpu_get_reg(REG_PC);
        pc += 3;

        while(pc != cpu_get_reg(REG_PC))
        {
            step6502();
        }
    }
    else
    {
        step6502();
    }
}

static void cmd_step(dbg_cxt_t *cxt, uint32_t num_params, cmd_param_t *params)
{
    step6502();
}

static void cmd_set_breakpoint(dbg_cxt_t *cxt, uint32_t num_params, cmd_param_t *params)
{
    uint8_t index;

    if(num_params == 0)
    {
        for(index = 0; index < MAX_BREAKPOINTS; ++index)
        {
            if(cxt->breakpoints[index] & BREAKPOINT_VALID_MASK)
            {
                printf("%2u: 0x%04x\n", index, cxt->breakpoints[index] & BREAKPOINT_ADDR_MASK);
            }
        }
    }
    else if(num_params >= 1 && params[0].int_valid)
    {
        for(index = 0; index < MAX_BREAKPOINTS; ++index)
        {
            if(!(cxt->breakpoints[index] & BREAKPOINT_VALID_MASK))
            {
                cxt->breakpoints[index] = (uint16_t)params[0].ival | BREAKPOINT_VALID_MASK;
                break;
            }
        }
    }
}

static void cmd_registers(dbg_cxt_t *cxt, uint32_t num_params, cmd_param_t *params)
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

static void cmd_test(dbg_cxt_t *cxt, uint32_t num_params, cmd_param_t *params)
{
    uint32_t i;
    printf("Num Params: %u\n", num_params);

    for(i=0; i<num_params; ++i)
    {
        printf("str:    %s\n", params[i].sval);
        printf("int ok: %s\n", params[i].int_valid?"TRUE":"FALSE");
        if(params[i].int_valid)
            printf("int:    %ld\n", params[i].ival);
    }
}

static dbg_cmd_t dbg_cmd_list[] = {
    { "continue", 'c', cmd_continue },
    { "next", 'n', cmd_next },
    { "step", 's', cmd_step },
    { "registers", 'r', cmd_registers },
    { "breakpoint", 'b', cmd_set_breakpoint },
    { "test", 't', cmd_test },
};

#define NUM_CMDS (sizeof(dbg_cmd_list)/sizeof(dbg_cmd_t))

static dbg_cmd_t *parse_cmd(char *input, uint32_t *num_params, cmd_param_t *params)
{
    dbg_cmd_t *ret = NULL;
    dbg_cmd_t *cmd;
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

    printf("[%s]\n", token);

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
            printf("p %s\n", token);
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

static bool dbg_eval_breakpoints(dbg_cxt_t *cxt, uint16_t pc)
{
    uint8_t i;
    for(i=0; i<MAX_BREAKPOINTS; ++i)
    {
        if(cxt->breakpoints[i] & BREAKPOINT_VALID_MASK && (cxt->breakpoints[i] & BREAKPOINT_ADDR_MASK) == pc)
            return true;
    }

    return false;
}

void debug_run(void)
{
    char disbuf[256];
    char inbuf[256];
    char *input;
    uint32_t num_params;
    cmd_param_t params[MAX_PARAMS];

    bool exit = false;
    dbg_cmd_t *cmd = NULL;

    cxt.broken = true;

    while(!exit)
    {
        if(cxt.broken)
        {
            disassemble(sizeof(disbuf), disbuf);
            printf("%s\n", disbuf);
            printf("cbemu>");
            fflush(stdout);
            input = fgets(inbuf, 256, stdin);

            if(input == NULL)
            {
                break;
            }

            input[strcspn(input, "\n")] = 0;

            if(strlen(input) == 0)
            {
                /* Repeat the last command */
                if(cmd != NULL)
                {
                    cmd->handler(&cxt, num_params, params);
                }
                continue;
            }

            cmd = parse_cmd(input, &num_params, params);

            if(cmd != NULL)
            {
                cmd->handler(&cxt, num_params, params);
            }
            else
            {
                printf("Unknown command\n");
            }
        }
        else
        {
            step6502();
        }

        if(dbg_eval_breakpoints(&cxt, cpu_get_reg(REG_PC)))
            cxt.broken = true;
    }
}
