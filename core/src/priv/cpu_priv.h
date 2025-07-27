#ifndef __CPU_PRIV_H__
#define __CPU_PRIV_H__

#include "emu_priv_types.h"

bool cpu_init(cbemu_t emu);
void cpu_tick(cbemu_t emu);

#endif /* end of include guard: __CPU_PRIV_H__ */
