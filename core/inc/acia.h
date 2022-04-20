#ifndef __ACIA_H__
#define __ACIA_H__

#include <stdint.h>
#include <stdbool.h>

#define ACIA_DEFAULT_SOCKNAME "acia.sock"

bool acia_init(char *socketpath);
void acia_write(uint8_t reg, uint8_t val);
uint8_t acia_read(uint8_t reg);
void acia_cleanup(void);

#endif /* end of include guard: __ACIA_H__ */
