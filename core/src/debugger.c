#include <bits/types/sigset_t.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* TODO portability from linux */
#include <strings.h>
#include <signal.h>

#include "debugger.h"
#include "cpu.h"

#define MAX_BREAKPOINTS 8
#define BREAKPOINT_VALID_MASK 0x80000000
#define BREAKPOINT_ADDR_MASK  0x0000ffff
#define FNV1a_OFFSET_BASIS 2166136261
#define FNV1a_PRIME        16777619
#define LABEL_TABLE_SIZE 1024
#define LABEL_ENTRIES_PER_BUCKET 4
#define MAX_LABEL_SIZE 64

#define CMD_DELIM " "
#define MAX_PARAMS 16

typedef struct dbg_label_s
{
    uint32_t key;
    char label[MAX_LABEL_SIZE];
    uint16_t address;
    bool used;
} dbg_label_t;

typedef struct dbg_cxt_s
{
    volatile bool broken;
    bool exit;
    uint32_t breakpoints[MAX_BREAKPOINTS];
    mem_space_t *mem_space;
    dbg_label_t labels[LABEL_TABLE_SIZE][LABEL_ENTRIES_PER_BUCKET];
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

static uint32_t hash_str(char *str)
{
    uint32_t hash = FNV1a_OFFSET_BASIS;

    while(*str != '\0')
    {
        hash ^= *str;
        hash = hash * FNV1a_PRIME;
        str++;
    }

    return hash;
}

static void add_label(char *name, uint16_t addr)
{
    dbg_label_t *label;
    uint32_t hash = hash_str(name);
    uint8_t i;
    uint32_t bucket = hash % LABEL_TABLE_SIZE;

    for(i=0;i<LABEL_ENTRIES_PER_BUCKET;++i)
    {
        if(!cxt.labels[bucket][i].used)
        {
            label = &cxt.labels[bucket][i];
            label->address = addr;
            strncpy(label->label, name, MAX_LABEL_SIZE);
            label->key = hash;
            label->used = true;
            break;
        }
        else if(cxt.labels[bucket][i].key == hash)
        {
            //printf("duplicate label hash: %s, %s\n", name, cxt.labels[bucket][i].label);
        }
    }

    if(i == LABEL_ENTRIES_PER_BUCKET)
    {
        fprintf(stderr, "Label bucket full: \n");
    }

    label->address = addr;
    strncpy(label->label, name, MAX_LABEL_SIZE);
    label->key = hash;
}

static dbg_label_t *find_label(char *name)
{
    uint32_t hash = hash_str(name);
    uint8_t i;
    uint32_t bucket = hash % LABEL_TABLE_SIZE;

    for(i=0;i<LABEL_ENTRIES_PER_BUCKET;++i)
    {
        if(cxt.labels[bucket][i].used && cxt.labels[bucket][i].key == hash)
        {
            return &cxt.labels[bucket][i];
        }
    }

    return NULL;
}

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
    uint16_t addr;
    dbg_label_t *label;
    bool valid = false;

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
    else if(num_params >= 1)
    {
        if(params[0].int_valid)
        {
            addr = (uint16_t)params[0].ival;
            valid = true;
        }
        else
        {
            label = find_label(params[0].sval);

            if(label != NULL)
            {
                addr = label->address;
                valid = true;
            }
        }
        for(index = 0; index < MAX_BREAKPOINTS && valid; ++index)
        {
            if(!(cxt->breakpoints[index] & BREAKPOINT_VALID_MASK))
            {
                cxt->breakpoints[index] = (uint32_t)addr | BREAKPOINT_VALID_MASK;
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

static void cmd_quit(dbg_cxt_t *cxt, uint32_t num_params, cmd_param_t *params)
{
    cxt->exit = true;
}

static void cmd_examine(dbg_cxt_t *cxt, uint32_t num_params, cmd_param_t *params)
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
        printf(" %02x", cxt->mem_space->read(addr+i));

        if(col == 16)
        {
            printf("\n");
            col = 0;
        }
    }

    if(col != 0)
        printf("\n");
}

static dbg_cmd_t dbg_cmd_list[] = {
    { "continue", 'c', cmd_continue },
    { "next", 'n', cmd_next },
    { "step", 's', cmd_step },
    { "registers", 'r', cmd_registers },
    { "breakpoint", 'b', cmd_set_breakpoint },
    { "test", 't', cmd_test },
    { "quit", 'q', cmd_quit },
    { "examine", 'x', cmd_examine },
};

#define NUM_CMDS (sizeof(dbg_cmd_list)/sizeof(dbg_cmd_t))

static void read_labels(FILE *lfile)
{
    char line[256];
    unsigned int addr;
    char name[256];

    while(fgets(line, sizeof(line), lfile) != NULL)
    {
        if(sscanf(line, "al %06X .%s", &addr, name) == 2)
        {
            if(strlen(name) > MAX_LABEL_SIZE || addr > 0xffff)
            {
                fprintf(stderr, "Invalid label line: %s", line);
                continue;
            }

            /* Ignore empty and local scope (@) labels. */
            if(name[0] != 0 && name[0] != '@')
                add_label(name, addr);
        }
    }
}

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

void sighandle(int s)
{
    cxt.broken = true;
}

void debug_run(mem_space_t *mem_space, char *labels_file)
{
    char disbuf[256];
    char inbuf[256];
    char *input;
    uint32_t num_params;
    cmd_param_t params[MAX_PARAMS];
    struct sigaction sigact;
    struct sigaction oldact;
    FILE *labels;

    dbg_cmd_t *cmd = NULL;

    cxt.broken = true;
    cxt.mem_space = mem_space;

    sigact.sa_flags = 0;
    sigemptyset(&sigact.sa_mask);
    sigact.sa_handler = sighandle;

    sigaction(SIGINT, &sigact, &oldact);

    if(labels_file != NULL)
    {
        printf("Reading labels from :%s\n", labels_file);

        labels = fopen(labels_file, "r");

        if(labels != NULL)
            read_labels(labels);
        else
            fprintf(stderr, "Unable to open labels file\n");
    }

    while(!cxt.exit)
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
                cxt.exit = true;
                continue;
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

    sigaction(SIGINT, &oldact, NULL);
    printf("\n");
}
