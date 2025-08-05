#ifndef __CPU_BIN_TYPE_H__
#define __CPU_BIN_TYPE_H__

#include <stdint.h>
#include <stdbool.h>

typedef struct
{
    uint16_t addr;
    uint8_t val;
    bool write;
} bus_result_t;

typedef struct
{
    const char *name;
    const char *file;
    uint8_t cycles;
    const bus_result_t *busops;
} cpu_bin_test_info_t;

extern const cpu_bin_test_info_t cpu_bin_tests[];
extern const uint32_t cpu_num_bin_tests;

#endif /* end of include guard: __CPU_BIN_TYPE_H__ */
