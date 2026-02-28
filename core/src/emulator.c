#include <stdlib.h>
#include <string.h>

#include "emulator.h"
#include "emu_priv_types.h"
#include "bus_priv.h"
#include "clock_priv.h"
#include "cpu_priv.h"

cbemu_t emu_init(const emu_config_t *config)
{
    cbemu_t emu;
    bool initst;

    if(config == NULL)
    {
        return NULL;
    }

    emu = malloc(sizeof(struct cbemu_s));

    memset(emu, 0, sizeof(struct cbemu_s));

    if(emu != NULL)
    {
        initst = bus_init(emu);

        if(initst)
            initst = clock_init(&emu->clk, &config->mainclk_config);

        if(initst)
            initst = cpu_init(emu);

        if(!initst)
        {
            emu_cleanup(emu);
            emu = NULL;
        }
    }

    return emu;
}

void emu_cleanup(cbemu_t emu)
{
    if(emu == NULL)
    {
        return;
    }

    bus_cleanup(emu);
    clock_cleanup(&emu->clk);

    free(emu);
}
