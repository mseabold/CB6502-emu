#ifndef __BITBANG_SPI_H__
#define __BITBANG_SPI_H__

#include "via.h"

bool bitbang_spi_init(via_t via);
void bitbang_spi_cleanup(void);

#endif /* end of include guard: __BITBANG_SPI_H__ */
