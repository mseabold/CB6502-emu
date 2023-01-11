/*
 * (c) 2022 Matt Seabold
 */
/**
 * @file
 * @brief Emulator debugger module
 */
#ifndef __DEBUGGER_H__
#define __DEBUGGER_H__

#include <stdint.h>
#include <stdbool.h>
#include "sys.h"
#include "dbginfo.h"

/**
 * Handle for a debugger instance.
 */
typedef struct debug_s *debug_t;

/**
 * Handle for a debugger breakpoint.
 */
typedef uint32_t debug_breakpoint_t;

/**
 * Special breakpoint handle indicating a blocking debugger call returned
 * due to a SW requested break rather than hitting a breakpoint or running to
 * completion.
 */
#define BREAKPOINT_HANDLE_SW_REQUEST 0xffffffff

/**
 * Structure representing information that can be queried about a debugger breakpoint.
 */
typedef struct breakpoint_info_s
{
    /**
     * The handle of the breakpoint.
     */
    uint32_t handle;

    /**
     * The address of the breakpoint.
     */
    uint16_t address;

    /**
     * The label associated with the breakpoint, or NULL if set directly by an address.
     * (Note: This is not currently used)
     */
    const char *label;
} breakpoint_info_t;

/**
 * Initializes a debugger instance
 *
 * @param[in] system_cxt Context of the system that is being debugged. This is used for memory bus access.
 *
 * @return The debugger instance, or NULL if there was a error.
 */
debug_t debug_init(sys_cxt_t system_cxt);

/**
 * Loads label information for an executing image. The file should be in the VICES label format,
 * as generated by the cc65 toolchain.
 *
 * @param[in] handle The debugger handle.
 * @param[in] labels_file The filename of the VICES label file to load.
 *
 * @return true if the labels were successfully loaded.
 */
bool debug_load_labels(debug_t handle, const char *labels_file);

/**
 * Sets an executable breakpoint directly on the given address.
 *
 * @param[in] handle The deubgger handle.
 * @param[out] breakpoint_handle On success, set to the handle of the created breakpoint.
 * @param[in] addr The address to set the breakpoint at.
 *
 * @return true if the breakpoint was set successfully.
 */
bool debug_set_breakpoint_addr(debug_t handle, debug_breakpoint_t *breakpoint_handle, uint16_t addr);

/**
 * Sets an executable breakpoint based on the specified label if present in a loaded labels file.
 *
 * @param[in] handle The deubgger handle.
 * @param[out] breakpoint_handle On success, set to the handle of the created breakpoint.
 * @param[in] label The label to set a breakpoint on.
 *
 * @retval true if the breakpoint was set successfully
 * @retval false if the breakpoint could not be set
 */
bool debug_set_breakpoint_label(debug_t handle, debug_breakpoint_t *breakpoint_handle, const char *label);

/**
 * Clears a previously set breakpoint.
 *
 * @param[in] handle The debugger handle.
 * @param[in] breakpoint_handle The handle of the breakpoint to clear
 *
 */
void debug_clear_breakpoint(debug_t handle, debug_breakpoint_t breakpoint_handle);

/**
 * Gets the currently active breakpoints.
 *
 * @param[in] handle The deubgger handle.
 * @param[in,out] num_breakpoints On entry, indicates the number of breakpoints available in the supplied buffer.
 *                                On return, indicates the number of breakpoints populated into the buffer.
 * @param[out] breakpoints Buffer of breakpoint information structures to be populated by the active breakpoints.
 * @param[out] total_breakpoints If supplied, populated with the total number of active breakpoints.
 */
void debug_get_breakpoints(debug_t handle, unsigned int *num_breakpoints, breakpoint_info_t *breakpoints, unsigned int *total_breakpoints);

/**
 * Performs a "next" operation. This executes the next opcode in the program being debugged. It also jumps over any subrountine
 * and executes them to completion, moving on to the next opcode in memory. During a subroutine, this function may return before
 * completion of a breakpoint is encountered while executing the subroutine.
 *
 * @param[in] handle The debugger handle.
 * @param[out] breakpoint_hit If this function returns @c true, this will be populated with the handle of the breakpoint that was hit.
 *
 * @return true if the debugger successfully executed until the next opcode in memory. false if it is returning early due to a breakpoint.
 */
bool debug_next(debug_t handle, debug_breakpoint_t *breakpoint_hit);

/**
 * Performs a "step" operation. This executes the next opcode logically in the program being debugged. This includes stepping into
 * any subroutine calls. This function will never consume more than one opcode.
 *
 * @param[in] handle The debugger handle.
 */
void debug_step(debug_t handle);

/**
 * Runs the emulator indefinitely until broken.
 *
 * @param[in] handle The debugger handle.
 * @param[out] breakpoint_hit Populated with the handle of the breakpoint that caused execution to stop.
 */
void debug_run(debug_t handle, debug_breakpoint_t *breakpoint_hit);

/**
 * Forced an external break (i.e. input from user to break).
 *
 * @param handle The debugger handle.
 */
void debug_break(debug_t handle);

/**
 * Execute until return for current subroutine.
 *
 * @param[in] handle The debugger handle.
 * @param[out] breakpoint_hit If this function returns @c true, this will be populated with the handle of the breakpoint that was hit.
 *
 * @return @c true if the debugger successfully executed until the next opcode in memory. @c false if it is returning early due to a breakpoint.
 */
bool debug_finish(debug_t handle, debug_breakpoint_t *breakpoint_hit);

/**
 * Provide the debugger with cc65 debug info for source file and symbol lookup.
 *
 * @param[in] handle The debugger handle.
 * @param[in] num_dbginfo The number if debug information files. Currently only 1 is supported
 * @param[in] dbginfo List if debug information files
 */
void debug_set_dbginfo(debug_t handle, unsigned int num_dbginfo, cc65_dbginfo *dbginfo);

#endif /* end of include guard: __DEBUGGER_H__ */
