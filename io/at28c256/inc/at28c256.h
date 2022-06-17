/*
 * (c) 2022 Matt Seabold
 */
#ifndef __AT28C256_H__
#define __AT28C256_H__

#include "sys.h"

typedef struct at28c256_s *at28c256_t;

#define AT28C256_INVALID_HANDLE       NULL
#define AT28C256_INIT_FLAG_ENABLE_SDP 0x00000001

at28c256_t at28c256_init(sys_cxt_t system_context, uint32_t flags);
void at28c256_destroy(at28c256_t handle);
bool at28c256_load_image(at28c256_t handle, uint16_t image_size, uint8_t *image, uint16_t offset);
void at28c256_write(at28c256_t handle, uint16_t addr, uint8_t val);
uint8_t at28c256_read(at28c256_t handle, uint16_t addr);
void at28c256_tick(at28c256_t handle, uint32_t ticks);


#endif /* end of include guard: __AT28C256_H__ */
