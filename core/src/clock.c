#include <string.h>
#include <stdlib.h>

#include "clock_priv.h"
#include "dbginfo.h"
#include "emu_priv_types.h"
#include "util.h"
#include "clock.h"
#include "log.h"

/**
 * Allocates and initializes a clock structure
 *
 * @param[in] freq  Frequency of the clock
 *
 * @return Handle for the allocated clock
 */
static clk_t clock_alloc_clk(clk_freq_t freq)
{
    clk_t clk;

    clk = malloc(sizeof(struct clk_s));
    if(clk != NULL)
    {
        memset(clk, 0, sizeof(struct clk_s));

        clk->freq = freq;
        clk->ticks = freq;
        list_init(&clk->callbacks);
    }

    return clk;
}

/**
 * Frees an allocated clock structure and its resources
 *
 * @param[in] clk   THe clock to free
 */
static void clock_free_clk(clk_t clk)
{
    /* Free the list of callbacks. This can be done with the generic list free function. */
    list_free_offset(&clk->callbacks, clk_cb_entry_t, node);

    /* Free the entry itself */
    free(clk);
}

/**
 * List free callback function for freeing a clock structure list entry. This is called
 * from the list free routine to clean up each clock list entry.
 *
 * @param[in] node  List entry node for the removed entry.
 * @param[in] param Unused for this handler
 */
static void clock_list_free_cb(listnode_t *node, void *param)
{
    clk_t clk;

    clk = (clk_t)list_container(node, struct clk_s, node);

    clock_free_clk(clk);
}

/**
 * Helper function for walking the list of tick callbacks for a given clock
 * and calling each of them.
 *
 * @param[in] clk   Clock to make tick callbacks for
 */
static void clock_make_callbacks(clk_t clk)
{
    listnode_t *node;
    clk_cb_entry_t *entry;

    list_iterate(&clk->callbacks, node)
    {
        entry = list_container(node, clk_cb_entry_t, node);
        entry->callback(clk, entry->userdata);
    }
}

/**
 * Initializes the clock module
 *
 * @param[in] cxt   Clock module context to initialize
 * @param[in] mainClkFreq   Frequency of the main bus clock (PHI2)
 *
 * @return true on successful initialization
 */
bool clock_init(clk_cxt_t *cxt, clk_freq_t mainClkFreq)
{
    bool result;

    memset(cxt, 0, sizeof(clk_cxt_t));
    list_init(&cxt->clks);
    cxt->mainClk = clock_alloc_clk(mainClkFreq);

    return (cxt->mainClk != NULL);
}

/**
 * Cleans up a given clock module context
 *
 * @param[in] cxt   Context to clean up
 */
void clock_cleanup(clk_cxt_t *cxt)
{
    if(cxt != NULL)
    {
        list_free(&cxt->clks, clock_list_free_cb, NULL);

        clock_free_clk(cxt->mainClk);
    }
}

/**
 * Tick the main bus clock. This will update and tick all other applicable clocks
 * based on relative frequency
 *
 * @param[in] cxt   Clock module context to tick the main clock for
 */
void clock_main_tick(clk_cxt_t *cxt)
{
    clk_t headClk;
    clk_freq_t remainingTicks = cxt->mainClk->freq;
    clk_freq_t ticksToConsume;
    bool rerunLoop = false;
    listnode_t *iter;

    if(list_empty(&cxt->clks))
    {
        /* Only the main clock exists, so just tick it. */
        clock_make_callbacks(cxt->mainClk);
    }
    else
    {
        while((remainingTicks > 0) || (rerunLoop))
        {
            rerunLoop = false;

            /* The head of the clock list is always the fewest remaining ticks. We will consume enough ticks
             * to either tick the main clock or tick this clock. If we tick this clock and still have time to consume,
             * we will loop back around and evaluate the next clock. */
            headClk = list_container(list_head(&cxt->clks), struct clk_s, node);

            if(remainingTicks >= headClk->ticks)
            {
                /* The head clock will tick before the main clock. Consume the time until this clock ticks
                 * and tick it. Remove the clock from the list. It will be added back in the appropriate place
                 * later as we consume the time for each clock. */
                ticksToConsume = headClk->ticks;
                list_remove(&headClk->node);
                headClk->ticks = headClk->freq;
                clock_make_callbacks(headClk);
            }
            else
            {
                /* Main clock will tick before any other clock. Just consume the time until the main clock ticks. */
                headClk = NULL;
                ticksToConsume = remainingTicks;
            }

            /* Walk the list of clocks */
            list_iterate(&cxt->clks, iter)
            {
                clk_t entry;

                entry = list_container(iter, struct clk_s, node);

                /* Update the consume time for this clock. */
                entry->ticks -= ticksToConsume;

                /* There is an edge case where there were more than one clock with the
                   same number of remaining ticks that exactly match the main clock tick.
                   In this case this clock is now at 0 ticks, but with 0 overall remaining
                   ticks we won't re-enter the loop. Set a specific flag to prevent that. */
                if(entry->ticks == 0)
                {
                    rerunLoop = true;
                }

                if((headClk != NULL) && (entry->ticks >= headClk->ticks))
                {
                    /* We removed the head clock after being ticked, so re-insert it back into
                       the appropriate sorted place. here */
                    list_insert_before(iter, &headClk->node);
                    headClk = NULL;
                }
            }

            if(headClk)
            {
                /* We walked the whole list without adding the old head clock, so
                   it must be the biggest one left. */
                list_add_tail(&cxt->clks, &headClk->node);
            }

            remainingTicks -= ticksToConsume;
        }

        /* All clocks with fewer remaining ticks than the main clock cycle have been
           ticked, so now tick the main clock. */
        clock_make_callbacks(cxt->mainClk);
    }
}

/**
 * Adds a clock to the core emulator.
 *
 * @param[in] emu   The emulator core
 * @parampin[ freq  The frequency of the clock to add
 *
 * @return A handle for the registered clock, or NULL on error.
 */
clk_t clock_add(cbemu_t emu, clk_freq_t freq)
{
    clk_t clk;
    clk_t listptr;
    listnode_t *node;
    bool added = false;

    clk = clock_alloc_clk(freq);

    if(clk != NULL)
    {
        list_iterate(&emu->clk.clks, node)
        {
            listptr = list_container(node, struct clk_s, node);

            if(listptr->ticks >= clk->ticks)
            {
                list_insert_before(node, &clk->node);
                added = true;
                break;
            }
        }

        if(!added)
        {
            list_add_tail(&emu->clk.clks, &clk->node);
        }
    }

    list_iterate(&emu->clk.clks, node)
    {
        listptr = list_container(node, struct clk_s, node);
        log_print(lDEBUG, "Clk %p, freq: %u\n", listptr, listptr->freq);
    }

    return clk;
}

/**
 * Removes a previously registerd clock
 *
 * @param[in] emu   The emulator core
 * @param[in] clk   The clock to remove
 */
void clock_remove(cbemu_t emu, clk_t clk)
{
    if((clk == NULL) || (emu->clk.mainClk == clk))
    {
        return;
    }

    list_remove(&clk->node);
    clock_free_clk(clk);
}

/**
 * Registers a tick handler for a given clock
 *
 * @param[in] clk       The clock for tick registration
 * @param[in] callback  The tick handler to be called on tick
 * @param[in] userdata  App specific data to be passed on each callback
 *
 * @return A handle for the registered callback or NULL on error
 */
clock_cb_handle_t clock_register_tick(clk_t clk, clock_tick_cb_t callback, void *userdata)
{
    clk_cb_entry_t *entry;

    if((clk == NULL) || (callback == NULL))
    {
        return NULL;
    }

    entry = malloc(sizeof(clk_cb_entry_t));

    if(entry != NULL)
    {
        entry->callback = callback;
        entry->userdata = userdata;
        list_add_tail(&clk->callbacks, &entry->node);
    }

    return entry;
}

/**
 * Un-registers a previously registered tack handler for a clock
 *
 * @param[in] handle    Handle of the registered callback to remove
 */
void clock_unregister_tick(clock_cb_handle_t handle)
{
    if(handle == NULL)
    {
        return;
    }

    list_remove(&((clk_cb_entry_t *)handle)->node);

    free(handle);
}
