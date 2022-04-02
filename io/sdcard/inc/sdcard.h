#ifndef __SDCARD_H__
#define __SDCARD_H__

#include <stdint.h>
#include <stdbool.h>

bool sdcard_init(const char *image_file);
void sdcard_spi_write(uint8_t byte);
uint8_t sdcard_spi_get(void);

#endif /* end of include guard: __SDCARD_H__ */
