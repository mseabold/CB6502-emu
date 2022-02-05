#ifndef __VIA_H__
#define __VIA_H__

#include <stdbool.h>
#include <stdint.h>

bool via_init(void);
void via_write(uint8_t reg, uint8_t val);
uint8_t via_read(uint8_t reg);

#endif /* __VIA_H__ */
