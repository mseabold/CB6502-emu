#include <stdio.h>
#include <string.h>

/* TODO portability from linux */
#include <strings.h>

#include "debugger.h"
#include "cpu.h"

#define MAX_BREAKPOINTS 8
#define BREAKPOINT_VALID_MASK 0x80000000
#define BREAKPOINT_ADDR_MASK  0x0000ffff

typedef struct dbg_cxt_s
{
    bool broken;
    uint32_t breakpoints[MAX_BREAKPOINTS];
} dbg_cxt_t;

static dbg_cxt_t cxt;

typedef void (*dbg_cmd_handler_t)(dbg_cxt_t *cxt);

typedef struct dbg_cmd_s
{
    const char *longcmd;
    char shortcmd;
    dbg_cmd_handler_t handler;
} dbg_cmd_t;

static void cmd_continue(dbg_cxt_t *cxt)
{
    cxt->broken = false;
}

static void cmd_next(dbg_cxt_t *cxt)
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

static void cmd_step(dbg_cxt_t *cxt)
{
    step6502();
}

static void cmd_set_breakpoint(dbg_cxt_t *cxt)
{
    uint8_t index;

    for(index = 0; index < MAX_BREAKPOINTS; ++index)
    {
        if(!(cxt->breakpoints[index] & BREAKPOINT_VALID_MASK))
        {
        }
    }
}

static void cmd_registers(dbg_cxt_t *cxt)
{
    cpu_regs_t regs;

    cpu_get_regs(&regs);
    printf("\tA: %02x\tS: %02x\n", regs.a, regs.sp);
    printf("\tX: %02x\tY: %02x\n", regs.x, regs.y);
    printf("\tPC: %04x\n", regs.pc);
}

static dbg_cmd_t dbg_cmd_list[] = {
    { "continue", 'c', cmd_continue },
    { "next", 'n', cmd_next },
    { "step", 's', cmd_step },
    { "registers", 'r', cmd_registers },
};

#define NUM_CMDS (sizeof(dbg_cmd_list)/sizeof(dbg_cmd_t))

static dbg_cmd_t *find_cmd(char *input)
{
    dbg_cmd_t *ret = NULL;
    dbg_cmd_t *cmd;
    size_t inputlen = strlen(input);
    int index;

    if(inputlen == 0)
        return NULL;

    for(index = 0; index < NUM_CMDS; ++index)
    {
        cmd = &dbg_cmd_list[index];
        if(inputlen == 1)
        {
            if(input[0] == cmd->shortcmd)
            {
                ret = cmd;
                break;
            }
        }
        else
        {
            if(strcasecmp(input, cmd->longcmd) == 0)
            {
                ret = cmd;
                break;
            }
        }
    }

    return ret;
}

void debug_run(void)
{
    char disbuf[256];
    char inbuf[256];
    char *input;

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
                    cmd->handler(&cxt);
                }
                continue;
            }

            cmd = find_cmd(input);

            if(cmd != NULL)
            {
                cmd->handler(&cxt);
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
    }
}
