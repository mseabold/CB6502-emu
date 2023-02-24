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

typedef enum
{
    IDLE,
    BYTE_LOAD,
    WRITE_CYCLE
} write_state_t;

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
};

at28c256_t at28c256_init(sys_cxt_t system_context, uint32_t flags)
{
    at28c256_t handle;

    handle = malloc(sizeof(struct at28c256_s));

    if(handle == NULL)
        return NULL;

    memset(handle, 0, sizeof(struct at28c256_s));
    handle->sys = system_context;
    handle->flags = flags;

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
    if(handle == NULL)
        return;

    addr &= IMAGE_MASK;

    switch(handle->write_state)
    {
        case IDLE:
            handle->page_mask = 0;

            handle->page_addr = addr & ADDR_PAGE_MASK;

            handle->write_state = BYTE_LOAD;

            /* Intentional fall through. */

        case BYTE_LOAD:
            /* TODO The datasheet does not seem document what the actual behavior here is. For now,
             *      just drop the write I guess. */
            if(handle->page_addr != (addr & ADDR_PAGE_MASK))
                return;

            handle->state_elapsed = 0;
            handle->last_write_addr = addr;
            handle->last_write = val;
            handle->page_mask |= ((uint64_t)1 << (addr & ADDR_BYTE_MASK));
            handle->page_buffer[addr & ADDR_BYTE_MASK] = val;

            break;
        case WRITE_CYCLE:
            /* Writes are blocked. */
            break;
    }
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
            case BYTE_LOAD:
                if(handle->state_elapsed + nanos >= T_BLC)
                {
                    nanos -= (T_BLC - handle->state_elapsed);
                    handle->write_state = WRITE_CYCLE;
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

                    for(i=0; i<PAGE_SIZE; ++i)
                    {
                        if(handle->page_mask & ((uint64_t)1 << i))
                        {
                            handle->image[handle->page_addr + i] = handle->page_buffer[i];
                        }
                    }

                    handle->write_state = IDLE;
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
