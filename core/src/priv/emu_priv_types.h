#ifndef __EMU_PRIV_TYPES_H__
#define __EMU_PRIV_TYPES_H__

#include "emu_types.h"
#include "bus_priv_types.h"
#include "clock_priv_types.h"
#include "cpu_priv_types.h"

/** Internal emulator context information. This maps to the main handle pointer type. */
struct cbemu_s
{
    bus_t bus;      /**< The emulator instance's bus instance. */
    clk_cxt_t clk;  /**< The emulator instance's clock context */
    cpu_t cpu;
};

#endif /* end of include guard: __EMU_PRIV_TYPES_H__ */
