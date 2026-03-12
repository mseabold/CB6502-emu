#ifndef __CLOCK_PRIV_TYPES__
#define __CLOCK_PRIV_TYPES__

#include "clock.h"
#include "util.h"

/**
 * Tracking structure for register clock callbacks.
 */
typedef struct
{
    clock_tick_cb_t callback;   /**< Callback function */
    void *userdata;             /**< App specific user data */
    listnode_t node;            /**< List entry node */
} clk_cb_entry_t;

/**
 * Tracking structure for registerd clocks
 */
struct clk_s
{
    clk_freq_t freq;            /**< Frequency of the clock */
    clk_period_t period;        /**< Period of the clock (in ns) */
    clk_period_t ticks;         /**< Remaining ns of the clock before it ticks */
    listnode_t callbacks;       /**< List head for registered callbacks. */
    listnode_t node;            /**< List entry node */
};

/**
 * Main clock module context
 */
typedef struct clk_cxt_s
{
    bool init;          /**< Indicates if the clock context has been initialized. */
    listnode_t clks;    /**< List head for registered clocks list. This is always sorted for lowest to highest remaining ticks. */
    clk_t mainClk;      /**< Main bus clock */
    clock_tick_cb_t main_hlr; /**< Internal handler for main clock ticks. */
} clk_cxt_t;

#endif /* end of include guard: __CLOCK_PRIV_TYPES__ */
