#ifndef __EMU_PRIV_TYPES_H__
#define __EMU_PRIV_TYPES_H__

#include "emu_types.h"
#include "bus_priv_types.h"
#include "clock_priv_types.h"

struct cbemu_s
{
    bus_t bus;
    clk_cxt_t clk;
};

#endif /* end of include guard: __EMU_PRIV_TYPES_H__ */
