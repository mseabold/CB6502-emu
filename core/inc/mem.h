/*
 * Copyright (c) 2022 Matt Seabold
 */
#ifndef __MEM_H__
#define __MEM_H__

#include <stdint.h>

typedef void (*mem_write_t)(uint16_t addr, uint8_t value);
typedef uint8_t (*mem_read_t)(uint16_t addr);

typedef struct mem_space_s
{
    mem_write_t write;
    mem_read_t read;
} mem_space_t;

#endif
