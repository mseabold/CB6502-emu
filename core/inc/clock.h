#ifndef __CLOCK_H__
#define __CLOCK_H__

#include <stdint.h>
#include <stdbool.h>
#include "emu_types.h"

/* For now, just support integer clock frequencies, but keep this a typedef in case
   we change that. */

/**
 * Clock frequency. This is represented in hertz.
 *
 * Note: for now the actual frequency is not meaningful as the emulator core does support
 * accurately enulated timing. However, this frequency is used as ratio between all registered
 * clocks in the system.
 */
typedef uint32_t clk_freq_t;

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
 * @param[in] emu   The emulator core
 * @parampin[ freq  The frequency of the clock to add
 *
 * @return A handle for the registered clock, or NULL on error.
 */
clk_t clock_add(cbemu_t emu, clk_freq_t freq);

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
