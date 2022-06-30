#ifndef __DEBUGGER_H__
#define __DEBUGGER_H__

#include <stdint.h>
#include <stdbool.h>
#include "sys.h"

typedef struct debug_s *debug_t;
typedef uint32_t debug_breakpoint_t;
#define BREAKPOINT_HANDLE_SW_REQUEST 0xffffffff

typedef struct breakpoint_info_s
{
    uint32_t handle;
    uint16_t address;
    const char *label;
} breakpoint_info_t;

debug_t debug_init(sys_cxt_t system_cxt);

bool debug_load_labels(debug_t handle, const char *labels_file);

bool debug_set_breakpoint_addr(debug_t handle, debug_breakpoint_t *breakpoint_handle, uint16_t addr);

bool debug_set_breakpoint_label(debug_t handle, debug_breakpoint_t *breakpoint_handle, const char *label);

void debug_clear_breakpoint(debug_t handle, debug_breakpoint_t breakpoint_handle);

void debug_get_breakpoints(debug_t handle, unsigned int *num_breakpoints, breakpoint_info_t *breakpoints, unsigned int *total_breakpoints);

bool debug_next(debug_t handle, debug_breakpoint_t *breakpoint_hit);

void debug_step(debug_t handle);

/**
 * Runs the emulator indefinitely until broken.
 */
void debug_run(debug_t handle, debug_breakpoint_t *breakpoint_hit);

/**
 * Forced an external break (i.e. input from user to break).
 *
 * @param handle Debugger handle.
 */
void debug_break(debug_t handle);

#endif /* end of include guard: __DEBUGGER_H__ */
