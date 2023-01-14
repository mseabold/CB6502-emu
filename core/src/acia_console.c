#include <stdio.h>

#include "acia.h"
#include "acia_console.h"

static void *acia_console_init(void *params)
{
    /* Just return non-NULL */
    return (void *)1;
}

static bool acia_console_available(void *handle)
{
    /* Console implementation currently does not support input. */
    return false;
}

static uint8_t acia_console_read(void *handle)
{
    return 0xff;
}

static void acia_console_write(void *handle, uint8_t data)
{
    printf("%c", (char)data);
}

static void acia_console_cleanup(void *handle)
{
}

static const acia_trans_interface_t acia_console_iface =
{
    acia_console_init,
    acia_console_available,
    acia_console_read,
    acia_console_write,
    acia_console_cleanup
};

const acia_trans_interface_t *acia_console_get_iface(void)
{
    return &acia_console_iface;
}

