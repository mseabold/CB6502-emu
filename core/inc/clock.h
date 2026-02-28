#ifndef __CLOCK_H__
#define __CLOCK_H__

#include <stdint.h>
#include <stdbool.h>
#include "emu_types.h"

/**
 * Maximum clock frequency represented by the minimum tick precision of 1ns.
 */
#define CLOCK_MAX_FREQUENCY 1000000000UL

/* For now, just support integer clock frequencies, but keep this a typedef in case
   we change that. */

/**
 * Defines the types of clock timing that can be used when defining a clock
 */
typedef enum
{
    CLOCK_FREQ, /**< Timing for a clock is given by a frequency. */
    CLOCK_PERIOD /**< Timing for a clock is fiven by its period. */
} clock_timing_type_t;

/**
 * Clock frequency. This is represented in hertz.
 *
 * Note: for now the actual frequency is not meaningful as the emulator core does support
 * accurately enulated timing. However, this frequency is used as ratio between all registered
 * clocks in the system.
 */
typedef uint32_t clk_freq_t;

/**
 * Clock period. This is represented in nanoseconds.
 *
 * Note that the minimummum resolution here is 1ns, which effectively meanas the maximum supported
 * frequency is 1GHz.
 */
typedef uint32_t clk_period_t;

/**
 * Defines the parameters for a clock instance
 */
typedef struct
{
    /** The type of timing data used for the clock. */
    clock_timing_type_t timing_type;

    /** The timing used for the clock, based on @ref timing_type. */
    union
    {
        clk_freq_t freq; /**< Timing data when @ref timing_type is @ref CLOCK_FREQ. */
        clk_period_t period; /**< Timing data when @ref timing_type is @ref FLOCK_PERIOD. */
    } timing;
} clock_config_t;

/** Handle for registered clocks. */
typedef struct clk_s *clk_t;

/** Handle for registered tick handlers. */
typedef void *clock_cb_handle_t;

/**
 * Callback prototype for handling ticks of a specified clock
 *
 * @param[in] clk       The clock that ticked.
 * @param[in] userdata  App-specific userdata for the registered callback
 */
typedef void (*clock_tick_cb_t)(clk_t clk, void *userdata);

/**
 * Adds a clock to the core emulator.
 *
 * @param[in] emu       The emulator core
 * @param[in] config    The clock's configuration parameters
 *
 * @return A handle for the registered clock, or NULL on error.
 */
clk_t clock_add(cbemu_t emu, const clock_config_t *config);

/**
 * Gets the core clock for the emulator.
 *
 * @param[in] emu   The emulator core.
 *
 * @return The handle for the core clock.
 */
clk_t clock_get_core_clk(cbemu_t);

/**
 * Removes a previously registerd clock
 *
 * @param[in] emu   The emulator core
 * @param[in] clk   The clock to remove
 */
void clock_remove(cbemu_t emu, clk_t clk);

/**
 * Registers a tick handler for a given clock
 *
 * @param[in] clk       The clock for tick registration
 * @param[in] callback  The tick handler to be called on tick
 * @param[in] userdata  App specific data to be passed on each callback
 *
 * @return A handle for the registered callback or NULL on error
 */
clock_cb_handle_t clock_register_tick(clk_t clk, clock_tick_cb_t callback, void *userdata);

/**
 * Un-registers a previously registered tack handler for a clock
 *
 * @param[in] handle    Handle of the registered callback to remove
 */
void clock_unregister_tick(clock_cb_handle_t handle);


#endif /* end of include guard: __CLOCK_H__ */
