/*
 * (c) 2022 Matt Seabold
 */
#include "at28c256.h"
#include "log.h"
#include <stdlib.h>
#include <string.h>

#define T_BLC ((uint64_t)150000)
#define T_WC  ((uint64_t)10000000)
#define PAGE_SIZE 64
#define ADDR_PAGE_MASK 0x7fc0
#define ADDR_BYTE_MASK 0x003f
#define IMAGE_SIZE 0x8000
#define IMAGE_MASK 0x7fff
#define IMAGE_FILL 0xff
#define STROBE_BIT 0x40
#define POLL_BIT   0x80

#define FLAG_SDP_ENABLED        0x00000001
#define FLAG_SDP_WRITES_ENABLED 0x00000002

#define SDP_WRITE_BLOCKED(_hndl) (((_hndl)->flags & (FLAG_SDP_ENABLED | FLAG_SDP_WRITES_ENABLED)) == FLAG_SDP_ENABLED)

typedef enum
{
    IDLE,
    SDP,
    SDP_WRITE_EN,
    BYTE_LOAD,
    WRITE_CYCLE
} write_state_t;

typedef enum
{
    /* No active SDP sequence. */
    SDP_IDLE,

    /* First two writes of SDP sequence are common between enable/disable */
    SDP0,
    SDP1,

    /* Enable sequence ends after SDP1, additional writes required for disable. */
    SDP_DIS_80,
    SDP_DIS_AA,
    SDP_DIS_55,

    /* The last 0x20 write will end the sequence so we don't need a state */
} sdp_seq_t;

typedef struct
{
    uint16_t addr;
    uint8_t val;
} sdp_exp_seq_t;

struct at28c256_s
{
    sys_cxt_t sys;
    uint8_t image[IMAGE_SIZE];
    uint32_t flags;
    uint8_t page_buffer[PAGE_SIZE];
    uint64_t page_mask;
    uint16_t page_addr;
    uint8_t last_write;
    uint16_t last_write_addr;
    write_state_t write_state;
    uint64_t state_elapsed;
    sdp_seq_t sdp_state;
};

/* Table of all expected writes for each SDP state. */
static const sdp_exp_seq_t sdp_seq_table[] =
{
    { 0x5555, 0xAA }, /* SDP_IDLE */
    { 0x2AAA, 0x55 }, /* SDP0 */
    { 0xFFFF, 0xFF }, /* SDP1. This is the only state with two possible values: 80 or A0 for disable/enable */
    { 0x5555, 0xAA }, /* SDP_DIS_80 */
    { 0x2AAA, 0x55 }, /* SDP_DIS_AA */
    { 0x5555, 0x20 }, /* SDP_DIS_55 */
};

static const char *write_state_dbg_str[] =
{
    "IDLE",
    "SDP",
    "SDP_WRITE_EN",
    "BYTE_LOAD",
    "WRITE_CYCLE"
};

static const char *sdp_state_dbg_str[] =
{
    "SDP_IDLE",
    "SDP0",
    "SDP1",
    "SDP_DIS_80",
    "SDP_DIS_AA",
    "SDP_DIS_55"
};

#define VALIDATE_SDP_WRITE(_state, _addr, _val) (((_addr) == sdp_seq_table[(_state)].addr) && ((_val) == sdp_seq_table[(_state)].val))

static inline void change_write_state(at28c256_t handle, write_state_t new_state)
{
    log_print(lDEBUG, "at28c256 write state: %s => %s", write_state_dbg_str[handle->write_state], write_state_dbg_str[new_state]);
    handle->write_state = new_state;
}

static inline void change_sdp_state(at28c256_t handle, sdp_seq_t new_state)
{
    log_print(lDEBUG, "at28c256 sdp state: %s => %s", sdp_state_dbg_str[handle->sdp_state], sdp_state_dbg_str[new_state]);
    handle->sdp_state = new_state;
}

#define incr_sdp_state(_hndl) change_sdp_state((_hndl), (_hndl)->sdp_state+1)

static void handle_page_write(at28c256_t handle, uint16_t addr, uint8_t val)
{
    handle->state_elapsed = 0;

    /* Once we've advanced beyond the initial SDP sequence write, we want to ignore all writes until the sequence
     * is complete.
     */
    if(handle->sdp_state <= SDP0)
    {
        handle->page_mask |= ((uint64_t)1 << (addr & ADDR_BYTE_MASK));
        handle->page_buffer[addr & ADDR_BYTE_MASK] = val;
    }
}

at28c256_t at28c256_init(sys_cxt_t system_context, uint32_t flags)
{
    at28c256_t handle;

    handle = malloc(sizeof(struct at28c256_s));

    if(handle == NULL)
        return NULL;

    memset(handle, 0, sizeof(struct at28c256_s));
    handle->sys = system_context;

    if(flags & AT28C256_INIT_FLAG_ENABLE_SDP)
    {
        handle->flags |= FLAG_SDP_ENABLED;
    }

    return handle;
}

void at28c256_destroy(at28c256_t handle)
{
    if(handle != NULL)
        free(handle);
}

bool at28c256_load_image(at28c256_t handle, uint16_t image_size, uint8_t *image, uint16_t offset)
{
    if(handle == NULL)
        return false;

    if((uint32_t)image_size + offset > IMAGE_SIZE)
        return false;

    if(image_size != IMAGE_SIZE)
        memset(handle->image, IMAGE_FILL, IMAGE_SIZE);

    memcpy(handle->image+offset, image, image_size);

    return true;
}

void at28c256_write(at28c256_t handle, uint16_t addr, uint8_t val)
{
    bool sdp_valid;

    if(handle == NULL)
        return;

    addr &= IMAGE_MASK;

    switch(handle->write_state)
    {
        case IDLE:
            if(addr == 0x5555 && val == 0xAA)
            {
                /* Could be an SDP sequence start, but *could* be a legit write
                 * to 0x5555. */
                change_sdp_state(handle, SDP0);
                change_write_state(handle, SDP);
            }
            else
            {
                change_write_state(handle, BYTE_LOAD);
            }

            handle->page_mask = 0;
            handle->page_addr = addr & ADDR_PAGE_MASK;

            handle_page_write(handle, addr, val);

            break;
        case SDP:
            sdp_valid = true;
            handle->state_elapsed = 0;

            if(handle->sdp_state == SDP1)
            {
                if(val != 0xA0 && val != 0x80)
                {
                    sdp_valid = false;
                }
            }
            else
            {
                sdp_valid = VALIDATE_SDP_WRITE(handle->sdp_state, addr, val);
            }

            if(sdp_valid)
            {
                if(handle->sdp_state == SDP1)
                {
                    /* This state his two possible valid write values, so we need to check manually. */
                    if(val == 0xA0)
                    {
                        /* SDP is now enabled, but writes are enabled until tBLC ends. */
                        handle->flags |= (FLAG_SDP_ENABLED | FLAG_SDP_WRITES_ENABLED);

                        /* Reset the page info, since we had originally set it up with the initial
                         * SDP sequence write in case it was a legitimate write. */
                        handle->page_mask = 0;

                        /* Move to WRITE_EN state. This will time out with tBLC, but
                         * any additional wite will start the BYTE_LOAD state. */
                        change_write_state(handle, SDP_WRITE_EN);
                        change_sdp_state(handle, SDP_IDLE);
                    }
                    else
                    {
                        /* State machine continues to disable SDP. */
                        change_sdp_state(handle, SDP_DIS_80);
                    }
                }
                else if(handle->sdp_state == SDP_DIS_55)
                {
                    /* Final write in the disable sequence was valid, so disable SDP. */
                    handle->flags &= ~FLAG_SDP_ENABLED;

                    /* SDP Sequence is over, but a write cycle is still active. Move to
                     * WRITE_EN state until another write occurs or tBLC expires. */
                    change_sdp_state(handle, SDP_IDLE);
                    change_write_state(handle, SDP_WRITE_EN);
                    handle->page_mask = 0;
                }
                else
                {
                    incr_sdp_state(handle);
                }
            }
            else
            {
                log_print(lWARNING, "Invalid SDP sequence. All previous sequence writes have been dropped.");
                change_sdp_state(handle, SDP_IDLE);
                change_write_state(handle, IDLE);

                /* We're done after the error. */
                return;
            }

            break;
        case SDP_WRITE_EN:
            /* We entered this state after an SDP sequence completed. If a write occurs during this state,
             * it's the first byte in a new page Write Cycle. So note the new mask and page address. */
            handle->state_elapsed = 0;
            handle->page_mask = 0;
            handle->page_addr = addr & ADDR_PAGE_MASK;

            handle_page_write(handle, addr, val);

            /* Now move to BYTE_LOAD state to handle any further page writes. */
            change_write_state(handle, BYTE_LOAD);
            break;
        case BYTE_LOAD:

            /* TODO The datasheet does not seem document what the actual behavior here is. For now,
             *      just drop the write I guess. */
            if(handle->page_addr != (addr & ADDR_PAGE_MASK))
            {
                log_print(lWARNING, "AT28C256: Write to different pages during same BLC. This is undefined behavior");
                return;
            }

            handle_page_write(handle, addr, val);

            break;
        case WRITE_CYCLE:
            /* Writes are blocked. */
            break;
    }

    /* Always track the last write for write cycle strobing. This even applies to SDP
     * state writes. */
    handle->last_write_addr = addr;
    handle->last_write = val;
}

uint8_t at28c256_read(at28c256_t handle, uint16_t addr)
{
    if(handle == NULL)
        return 0xff;

    if(handle->write_state != IDLE && addr == handle->last_write_addr)
    {
        /* Toggle bit 6. */
        handle->last_write ^= STROBE_BIT;

        /* Complement bit 7 in return. */
        return (handle->last_write ^ POLL_BIT);
    }

    return handle->image[addr & IMAGE_MASK];
}

void at28c256_tick(at28c256_t handle, uint32_t ticks)
{
    uint64_t nanos;
    uint8_t i;

    if(handle == NULL)
        return;

    nanos = sys_convert_ticks_to_ns(handle->sys, ticks);

    while(nanos > 0)
    {
        switch(handle->write_state)
        {
            case IDLE:
                nanos = 0;
                break;
            case SDP:
            case SDP_WRITE_EN:
            case BYTE_LOAD:
                if(handle->state_elapsed + nanos >= T_BLC)
                {
                    if(handle->write_state == SDP)
                    {
                        log_print(lWARNING, "tBLC expired during SDP sequence. This is means SW did not complete an SDP sequence. This is undefined behavior");
                        handle->sdp_state = SDP_IDLE;
                    }

                    /* tBLC expiration is handled the same regardless of SDP state now. Start the write cycle. */
                    nanos -= (T_BLC - handle->state_elapsed);
                    change_write_state(handle, WRITE_CYCLE);
                    handle->state_elapsed = 0;
                }
                else
                {
                    handle->state_elapsed += nanos;
                    nanos = 0;
                }
                break;
            case WRITE_CYCLE:
                if(handle->state_elapsed + nanos >= T_WC)
                {
                    nanos -= (T_WC - handle->state_elapsed);

                    /* BLC and WC work as normal when SDP is enabled. SDP is only respected now,
                     * when the actual write occurs. Only perform the write if they aren't blocked
                     * by SDP. */
                    if(!SDP_WRITE_BLOCKED(handle))
                    {
                        /* SDP is blocking writes, and this is not an SDP sequence start,
                         * so nothing to do. */
                        for(i=0; i<PAGE_SIZE; ++i)
                        {
                            if(handle->page_mask & ((uint64_t)1 << i))
                            {
                                handle->image[handle->page_addr + i] = handle->page_buffer[i];
                            }
                        }
                    }

                    /* After WC ends, temporary write enablement after SDP sequence is disabled. */
                    handle->flags &= ~FLAG_SDP_WRITES_ENABLED;

                    change_write_state(handle, IDLE);
                    handle->state_elapsed = 0;
                }
                else
                {
                    handle->state_elapsed += nanos;
                    nanos = 0;
                }
                break;
        }
    }
}

void at28c256_set_sdp_enable(at28c256_t handle, bool enable)
{
    if(handle == NULL)
    {
        return;
    }

    /* Don't allow SDP changes while a cycle is in progress (to prevent odd timing things that
     * can't happen in real life). */
    if(handle->write_state != IDLE)
    {
        log_print(lNOTICE, "Manual SDP state change not allowed while a write cycle is in progress.");
        return;
    }

    if(enable)
    {
        handle->flags |= FLAG_SDP_ENABLED;
    }
    else
    {
        handle->flags &= ~FLAG_SDP_ENABLED;
    }
}
