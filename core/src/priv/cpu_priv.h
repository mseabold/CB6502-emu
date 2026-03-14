#ifndef __CPU_PRIV_H__
#define __CPU_PRIV_H__

#include "emu_priv_types.h"

#define CPU_GET_REG(_emu, _reg)   (_emu)->cpu.regs._reg

bool cpu_init(cbemu_t emu);
void cpu_tick(cbemu_t emu);

bool cpu_is_subroutine(cbemu_t emu);

/* TODO this is just to enable the tester for now. */
uint16_t cpu_get_pc(cbemu_t emu);
bool cpu_is_sync(cbemu_t emu);

#endif /* end of include guard: __CPU_PRIV_H__ */
