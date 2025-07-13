#ifndef __BUS_PRIV_H__
#define __BUS_PRIV_H__

#include "emu_priv_types.h"

bool bus_init(bus_t *bus);
void bus_cleanup(bus_t *bus);

uint8_t bus_read(bus_t *bus, uint16_t addr);
uint8_t bus_peek(bus_t *bus, uint16_t addr);
void bus_write(bus_t *bus, uint16_t addr, uint8_t value);

#endif /* end of include guard: __BUS_PRIV_H__ */
