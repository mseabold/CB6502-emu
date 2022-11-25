#ifndef __CB6502_H__
#define __CB6502_H__

#include "sys.h"

/* TODO In the future this may need to return a context for multiple instances. */
bool cb6502_init(const char *rom_file, const char *acia_socket);
sys_cxt_t cb6502_get_sys(void);
void cb6502_destroy(void);

#endif /* end of include guard: __CB6502_H__ */
