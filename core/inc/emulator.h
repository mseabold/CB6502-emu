#ifndef __EMULATOR_H__
#define __EMULATOR_H__

#include "emu_types.h"
#include "bus.h"
#include "clock.h"

typedef struct
{
    clk_freq_t main_freq;
} emu_config_t;

cbemu_t emu_init(emu_config_t *config);
void emu_cleanup(cbemu_t emu);

#endif /* end of include guard: __EMULATOR_H__ */
