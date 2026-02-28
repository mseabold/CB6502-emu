#ifndef __CLOCK_PRIV_H__
#define __CLOCK_PRIV_H__

#include "clock_priv_types.h"
#include "clock.h"

/**
 * Initializes the clock module
 *
 * @param[in] cxt       Clock module context to initialize
 * @param[in] config    The clock's configuration parameters
 *
 * @return true on successful initialization
 */
bool clock_init(clk_cxt_t *cxt, const clock_config_t *config);

/**
 * Cleans up a given clock module context
 *
 * @param[in] cxt   Context to clean up
 */
void clock_cleanup(clk_cxt_t *cxt);

/**
 * Tick the main bus clock. This will update and tick all other applicable clocks
 * based on relative frequency
 *
 * @param[in] cxt   Clock module context to tick the main clock for
 */
void clock_main_tick(clk_cxt_t *cxt);

#endif /* end of include guard: __CLOCK_PRIV_H__ */
