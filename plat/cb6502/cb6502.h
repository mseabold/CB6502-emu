#ifndef __CB6502_H__
#define __CB6502_H__

#include <stdbool.h>
#include "emulator.h"

/* TODO In the future this may need to return a context for multiple instances. */
bool cb6502_init(const char *rom_file, const char *acia_socket, cbemu_t *emulator);
void cb6502_destroy(void);

#endif /* end of include guard: __CB6502_H__ */
