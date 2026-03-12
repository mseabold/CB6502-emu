#ifndef __EMULATOR_H__
#define __EMULATOR_H__

#include "emu_types.h"
#include "bus.h"
#include "clock.h"

typedef struct
{
    clock_config_t mainclk_config;
} emu_config_t;

cbemu_t emu_init(const emu_config_t *config);
void emu_cleanup(cbemu_t emu);
void emu_tick(cbemu_t emu);

#endif /* end of include guard: __EMULATOR_H__ */
