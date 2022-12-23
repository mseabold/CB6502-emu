#include <bits/types/sigset_t.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* TODO portability from linux */
#include <strings.h>
#include <signal.h>

#include "debugger.h"
#include "cpu.h"
#include "sys.h"

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
    uint32_t hash;
    char label[MAX_LABEL_SIZE];
    uint16_t address;
    bool used;
} dbg_label_t;

struct debug_s
{
    volatile bool sw_break;
    bool exit;
    uint32_t breakpoints[MAX_BREAKPOINTS];
    sys_cxt_t syscxt;
    dbg_label_t labels[LABEL_TABLE_SIZE][LABEL_ENTRIES_PER_BUCKET];
};

static uint32_t hash_str(const char *str)
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

static void add_label(debug_t handle, char *name, uint16_t addr)
{
    dbg_label_t *label;
    uint32_t hash = hash_str(name);
    uint8_t i;
    uint32_t bucket = hash % LABEL_TABLE_SIZE;

    for(i=0;i<LABEL_ENTRIES_PER_BUCKET;++i)
    {
        if(!handle->labels[bucket][i].used)
        {
            label = &handle->labels[bucket][i];
            label->address = addr;
            strncpy(label->label, name, MAX_LABEL_SIZE);
            label->hash = hash;
            label->used = true;
            break;
        }
        else if(handle->labels[bucket][i].hash == hash)
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
    label->hash = hash;
}

static dbg_label_t *find_label(debug_t handle, const char *name)
{
    uint32_t hash = hash_str(name);
    uint8_t i;
    uint32_t bucket = hash % LABEL_TABLE_SIZE;

    for(i=0;i<LABEL_ENTRIES_PER_BUCKET;++i)
    {
        if(handle->labels[bucket][i].used && handle->labels[bucket][i].hash == hash && !strncmp(name, handle->labels[bucket][i].label, MAX_LABEL_SIZE))
        {
            return &handle->labels[bucket][i];
        }
    }

    return NULL;
}

static bool dbg_eval_breakpoints(debug_t handle, uint16_t pc, debug_breakpoint_t *bphandle)
{
    uint8_t i;
    for(i=0; i<MAX_BREAKPOINTS; ++i)
    {
        if(handle->breakpoints[i] & BREAKPOINT_VALID_MASK && (handle->breakpoints[i] & BREAKPOINT_ADDR_MASK) == pc)
        {
            if(bphandle)
                *bphandle = (debug_breakpoint_t)i;

            return true;
        }
    }

    return false;
}

debug_t debug_init(sys_cxt_t system_cxt)
{
    debug_t handle;

    if(system_cxt == NULL)
        return NULL;

    handle = malloc(sizeof(struct debug_s));

    if(handle == NULL)
        return NULL;

    memset(handle, 0, sizeof(struct debug_s));

    handle->syscxt = system_cxt;

    return handle;
}

bool debug_load_labels(debug_t handle, const char *labels_file)
{
    char line[256];
    unsigned int addr;
    char name[256];
    FILE *lfile;

    lfile = fopen(labels_file, "r");

    if(lfile == NULL)
        return false;

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
                add_label(handle, name, addr);
        }
    }

    fclose(lfile);

    return true;
}

bool debug_set_breakpoint_addr(debug_t handle, debug_breakpoint_t *breakpoint_handle, uint16_t addr)
{
    unsigned int index;

    if(handle == NULL || breakpoint_handle == NULL)
        return false;

    for(index = 0; index < MAX_BREAKPOINTS; ++index)
    {
        if(!(handle->breakpoints[index] & BREAKPOINT_VALID_MASK))
        {
            handle->breakpoints[index] = (uint32_t)addr | BREAKPOINT_VALID_MASK;

            *breakpoint_handle = (debug_breakpoint_t)index;

            return true;
        }
    }

    return false;
}

bool debug_set_breakpoint_label(debug_t handle, debug_breakpoint_t *breakpoint_handle, const char *label)
{
    unsigned int index;
    dbg_label_t *label_info;

    if(handle == NULL || breakpoint_handle == NULL)
        return false;

    label_info = find_label(handle, label);

    if(label_info == NULL)
        return false;

    for(index = 0; index < MAX_BREAKPOINTS; ++index)
    {
        if(!(handle->breakpoints[index] & BREAKPOINT_VALID_MASK))
        {
            handle->breakpoints[index] = (uint32_t)label_info->address | BREAKPOINT_VALID_MASK;

            *breakpoint_handle = (debug_breakpoint_t)index;

            return true;
        }
    }

    return false;
}

void debug_clear_breakpoint(debug_t handle, debug_breakpoint_t breakpoint_handle)
{
    unsigned int index;

    if(handle == NULL)
        return;

    for(index = 0; index < MAX_BREAKPOINTS; ++index)
    {
        handle->breakpoints[index] &= ~BREAKPOINT_VALID_MASK;
    }
}

void debug_get_breakpoints(debug_t handle, unsigned int *num_breakpoints, breakpoint_info_t *breakpoints, unsigned int *total_breakpoints)
{
    unsigned int index, out_index;

    if(total_breakpoints)
        *total_breakpoints = 0;

    if(handle == NULL)
        return;

    for(index = 0, out_index = 0; index < MAX_BREAKPOINTS; ++index)
    {
        if(handle->breakpoints[index] & BREAKPOINT_VALID_MASK)
        {
            if(total_breakpoints)
                ++(*total_breakpoints);

            if(num_breakpoints && breakpoints && out_index < *num_breakpoints)
            {
                breakpoints[out_index].address = handle->breakpoints[index] & BREAKPOINT_ADDR_MASK;
                breakpoints[out_index].handle = (debug_breakpoint_t)index;
                breakpoints[out_index].label = NULL; //TODO
                ++out_index;
            }
        }
    }

    if(num_breakpoints)
        *num_breakpoints = out_index;
}

bool debug_next(debug_t handle, debug_breakpoint_t *breakpoint_hit)
{
    uint16_t pc,next_pc;

    if(handle == NULL)
        return false;

    if(cpu_is_subroutine())
    {
        next_pc = cpu_get_reg(REG_PC) + 3;

        pc = cpu_get_reg(REG_PC);

        handle->sw_break = false;
        while(!handle->sw_break && next_pc != cpu_get_reg(REG_PC))
        {
            cpu_step();

            if(dbg_eval_breakpoints(handle, pc, breakpoint_hit))
            {
                return true;
            }

            pc = cpu_get_reg(REG_PC);
        }

        if(handle->sw_break)
        {
            if(breakpoint_hit)
                *breakpoint_hit = BREAKPOINT_HANDLE_SW_REQUEST;

            return true;
        }
    }
    else
        cpu_step();

    return false;
}

void debug_step(debug_t handle)
{
    if(handle == NULL)
        return;

    cpu_step();
}

void debug_run(debug_t handle, debug_breakpoint_t *breakpoint_hit)
{
    if(handle == NULL)
        return;

    handle->sw_break = false;

    while(!handle->sw_break)
    {
        if(dbg_eval_breakpoints(handle, cpu_get_reg(REG_PC), breakpoint_hit))
        {
            return;
        }
        else
        {
            cpu_step();
        }
    }

    if(handle->sw_break && breakpoint_hit)
        *breakpoint_hit = BREAKPOINT_HANDLE_SW_REQUEST;
}

void debug_break(debug_t handle)
{
    if(handle != NULL)
        handle->sw_break = true;
}

bool debug_finish(debug_t handle, debug_breakpoint_t *breakpoint_hit)
{
    uint8_t opcode;
    bool is_ret = false;
    uint16_t pc;

    if(handle == NULL)
        return false;

    while(!is_ret && !handle->sw_break)
    {
        pc = cpu_get_reg(REG_PC);

        if(dbg_eval_breakpoints(handle, pc, breakpoint_hit))
        {
            return true;
        }

        opcode = sys_peek_mem(handle->syscxt, cpu_get_reg(REG_PC));

        if(opcode == 0x40 || opcode == 0x60)
        {
            is_ret = true;
        }

        cpu_step();
    }

    if(handle->sw_break && breakpoint_hit != NULL)
    {
        *breakpoint_hit = BREAKPOINT_HANDLE_SW_REQUEST;
        return true;
    }

    return false;
}
