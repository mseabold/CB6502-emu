#ifndef __ACIA_H__
#define __ACIA_H__

#include <stdint.h>
#include <stdbool.h>

bool acia_init(void);
void acia_write(uint8_t reg, uint8_t val);
uint8_t acia_read(uint8_t reg);

#endif /* end of include guard: __ACIA_H__ */
