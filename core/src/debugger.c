#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* TODO portability from linux */
#include <strings.h>

#include "dbginfo.h"
#include "debugger.h"
#include "log.h"
#include "cpu_priv.h"
#include "bus_priv.h"

#define MAX_BREAKPOINTS 8
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

typedef struct
{
    bool used;
    bool sym_valid;
    uint16_t addr;
    dbg_label_t *label;
    cc65_symboldata sym;
} breakpoint_t;

struct debug_s
{
    cbemu_t emu;
    volatile bool sw_break;
    bool exit;
    breakpoint_t breakpoints[MAX_BREAKPOINTS];
    dbg_label_t labels[LABEL_TABLE_SIZE][LABEL_ENTRIES_PER_BUCKET];
    cc65_dbginfo dbginfo;
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
        if(handle->breakpoints[i].used && handle->breakpoints[i].addr == pc)
        {
            if(bphandle)
                *bphandle = (debug_breakpoint_t)i;

            return true;
        }
    }

    return false;
}

static void debug_step_i(debug_t handle)
{
    do
    {
        emu_tick(handle->emu);
    } while(handle->emu->cpu.op_state != OPCODE);
}


debug_t debug_init(cbemu_t emulator)
{
    debug_t handle;

    if(emulator == NULL)
    {
        return NULL;
    }

    handle = malloc(sizeof(struct debug_s));

    if(handle == NULL)
    {
        return NULL;
    }

    memset(handle, 0, sizeof(struct debug_s));

    handle->emu = emulator;

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
        if(!handle->breakpoints[index].used)
        {
            handle->breakpoints[index].used = true;
            handle->breakpoints[index].addr = addr;
            handle->breakpoints[index].label = NULL;

            *breakpoint_handle = (debug_breakpoint_t)index;

            return true;
        }
    }

    return false;
}

bool debug_set_breakpoint_label(debug_t handle, debug_breakpoint_t *breakpoint_handle, const char *label)
{
    unsigned int index, symindex;
    dbg_label_t *label_info;
    const cc65_symbolinfo *syminfo;

    if(handle == NULL || breakpoint_handle == NULL)
        return false;

    for(index = 0; index < MAX_BREAKPOINTS; ++index)
    {
        if(!handle->breakpoints[index].used)
            break;
    }

    if(index == MAX_BREAKPOINTS)
    {
        return false;
    }

    if(handle->dbginfo != NULL)
    {
        syminfo = cc65_symbol_byname(handle->dbginfo, label);

        if(syminfo != NULL)
        {
            for(symindex = 0; symindex < syminfo->count; ++symindex)
            {
                /* Find a label symbol. If there are multiple, for now
                 * we'll just pick the first one we see. */
                if(syminfo->data[symindex].symbol_type == CC65_SYM_LABEL)
                {
                    break;
                }
            }

            if(symindex < syminfo->count)
            {
                /* The name pointer is kept valid for the lifetime of the debug info,
                 * so it's safe to just copy the data out. */
                handle->breakpoints[index].used = true;
                handle->breakpoints[index].addr = syminfo->data[symindex].symbol_value;
                handle->breakpoints[index].label = NULL;
                handle->breakpoints[index].sym_valid = true;
                handle->breakpoints[index].sym = syminfo->data[symindex];

                cc65_free_symbolinfo(handle->dbginfo, syminfo);
                return true;
            }

            cc65_free_symbolinfo(handle->dbginfo, syminfo);
        }

        /* Fall through and check the label hash table if no symbol could be found. */
    }

    label_info = find_label(handle, label);

    if(label_info == NULL)
        return false;

    for(index = 0; index < MAX_BREAKPOINTS; ++index)
    {
        if(!handle->breakpoints[index].used)
        {
            handle->breakpoints[index].used = true;
            handle->breakpoints[index].addr = label_info->address;
            handle->breakpoints[index].label = label_info;

            *breakpoint_handle = (debug_breakpoint_t)index;

            return true;
        }
    }

    return false;
}

void debug_clear_breakpoint(debug_t handle, debug_breakpoint_t breakpoint_handle)
{
    unsigned int index;

    if(handle == NULL || breakpoint_handle >= MAX_BREAKPOINTS)
        return;

    handle->breakpoints[breakpoint_handle].used = false;
    handle->breakpoints[breakpoint_handle].label = NULL;
    handle->breakpoints[breakpoint_handle].sym_valid = false;
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
        if(handle->breakpoints[index].used)
        {
            if(total_breakpoints)
                ++(*total_breakpoints);

            if(num_breakpoints && breakpoints && out_index < *num_breakpoints)
            {
                breakpoints[out_index].address = handle->breakpoints[index].addr;
                breakpoints[out_index].handle = (debug_breakpoint_t)index;

                if(handle->breakpoints[index].label != NULL)
                {
                    breakpoints[out_index].label = handle->breakpoints[index].label->label;
                }
                else if(handle->breakpoints[index].sym_valid)
                {
                    breakpoints[out_index].label = handle->breakpoints[index].sym.symbol_name;
                }
                else
                {
                    breakpoints[out_index].label = NULL;
                }

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

    if(cpu_is_subroutine(handle->emu))
    {
        pc = handle->emu->cpu.regs.pc;
        next_pc = pc + 3;

        handle->sw_break = false;
        while(!handle->sw_break && next_pc != CPU_GET_REG(handle->emu, pc))
        {
            debug_step_i(handle);

            if(dbg_eval_breakpoints(handle, pc, breakpoint_hit))
            {
                return true;
            }

            pc = CPU_GET_REG(handle->emu, pc);
        }

        if(handle->sw_break)
        {
            if(breakpoint_hit)
                *breakpoint_hit = BREAKPOINT_HANDLE_SW_REQUEST;

            return true;
        }
    }
    else
        debug_step_i(handle);

    return false;
}

void debug_step(debug_t handle)
{
    if(handle == NULL)
        return;

    debug_step_i(handle);
}

void debug_run(debug_t handle, debug_breakpoint_t *breakpoint_hit)
{
    if(handle == NULL)
        return;

    handle->sw_break = false;

    while(!handle->sw_break)
    {
        if(dbg_eval_breakpoints(handle, CPU_GET_REG(handle->emu, pc), breakpoint_hit))
        {
            return;
        }
        else
        {
            debug_step_i(handle);
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
    unsigned int subroutine_count = 0;

    if(handle == NULL)
        return false;

    while(!is_ret && !handle->sw_break)
    {
        pc = CPU_GET_REG(handle->emu, pc);

        if(dbg_eval_breakpoints(handle, pc, breakpoint_hit))
        {
            return true;
        }

        opcode = bus_peek(handle->emu, pc);

        if(opcode == 0x20)
        {
            ++subroutine_count;
        }
        else if((opcode == 0x40 || opcode == 0x60))
        {
            if(subroutine_count > 0)
            {
                --subroutine_count;
            }
            else
            {
                is_ret = true;
            }
        }

        debug_step_i(handle);
    }

    if(handle->sw_break && breakpoint_hit != NULL)
    {
        *breakpoint_hit = BREAKPOINT_HANDLE_SW_REQUEST;
        return true;
    }

    return false;
}

void debug_set_dbginfo(debug_t handle, unsigned int num_dbginfo, cc65_dbginfo *dbginfo)
{
    unsigned int index;

    if(handle == NULL || num_dbginfo > 1)
    {
        return;
    }

    if(num_dbginfo == 0)
    {
        for(index = 0; index < MAX_BREAKPOINTS; ++index)
        {
            handle->breakpoints[index].sym_valid = false;
        }

        handle->dbginfo = NULL;
    }
    else
    {
        handle->dbginfo = *dbginfo;
    }
}

/**
 * Retrieve all of internal CPU register states
 *
 * @param[in] handle    The debugger handle.
 * @param[out] regs     Register info to populate.
 */
void debug_get_cpu_regs(debug_t handle, debug_cpu_regs_t *regs)
{
    if((handle == NULL) || (regs == NULL))
    {
        return;
    }

    /* Register structure is intentionally the same. */
    memcpy(regs, &handle->emu->cpu.regs, sizeof(debug_cpu_regs_t));
}

/**
 * Peeks a memory address using the debugger.
 *
 * @param[in] handle    The debugger handle.
 * @param[in] addr      The address to peek.
 *
 * @return The peeked value at the address.
 */
uint8_t debug_peek(debug_t handle, uint16_t addr)
{
    if(handle == NULL)
    {
        return 0xFF;
    }

    return bus_peek(handle->emu, addr);
}

/**
 * Dumps a section of the emulator memory space using the debugger.
 *
 * @param[in] handle    The debugger handle.
 * @param[in] addr      The base address to begin the dump at.
 * @param[in,out] len   As input, the length of memory data to dump from the base address.
 *                      On output, the amount of data actually copied into the buffer.
 * @param[out] buffer   Buffer to dump the memory data into.
 */
void debug_dump(debug_t handle, uint16_t addr, uint16_t *len, uint8_t *buffer)
{
    uint32_t index;
    uint32_t end_addr;

    if((handle == NULL) || (len == NULL) || (*len == 0) || (buffer == NULL))
    {
        return;
    }

    end_addr = (uint32_t)addr + (uint32_t)*len;

    /* Limit end_addr if len was too big and it rolled over. */
    if(end_addr > 0x10000)
    {
        end_addr = 0x10000;
    }

    for(index = addr; index < end_addr; index++)
    {
        buffer[index-addr] = bus_peek(handle->emu, index);
    }

    *len = (uint16_t)(end_addr - addr);
}
