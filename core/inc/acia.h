#ifndef __ACIA_H__
#define __ACIA_H__

#include <stdint.h>
#include <stdbool.h>

#define ACIA_DEFAULT_SOCKNAME "acia.sock"

typedef struct acia_s *acia_t;

acia_t acia_init(char *socketpath);
void acia_write(acia_t handle, uint8_t reg, uint8_t val);
uint8_t acia_read(acia_t handle, uint8_t reg);
void acia_cleanup(acia_t handle);

#endif
