#ifndef __CLOCK_PRIV_H__
#define __CLOCK_PRIV_H__

#include "clock_priv_types.h"
#include "clock.h"

/**
 * Initializes the clock module
 *
 * @param[in] emu       The main emulator context to initialize.
 * @param[in] config    The clock's configuration parameters
 * @param[in] main_clk_hlr  Dedicated internal handler for the main clock. This will be called prior to making
 *                          any registered callbacks on the main clock. This allows the emulator core to perform
 *                          cpu/other internal module ticking prior to the "rest of the world" receives the tick.
 *
 * @return true on successful initialization
 */
bool clock_init(cbemu_t emu, const clock_config_t *config, clock_tick_cb_t main_clk_hlr);

/**
 * Cleans up a given clock module context
 *
 * @param[in] emu   The main emulator context to clean up.
 */
void clock_cleanup(cbemu_t emu);

/**
 * Tick the main bus clock. This will update and tick all other applicable clocks
 * based on relative frequency
 *
 * @param[in] emu   The main emulator context to tick.
 */
void clock_main_tick(cbemu_t emu);

#endif /* end of include guard: __CLOCK_PRIV_H__ */
