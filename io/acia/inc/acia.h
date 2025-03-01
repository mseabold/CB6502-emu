#ifndef __ACIA_H__
#define __ACIA_H__

#include <stdint.h>
#include <stdbool.h>
#include "sys.h"

#define ACIA_DEFAULT_SOCKNAME "acia.sock"

typedef void *(*acia_trans_init_t)(void *params);
typedef bool (*acia_trans_available_t)(void *handle);
typedef uint8_t (*acia_trans_read_t)(void *handle);
typedef void (*acia_trans_write_t)(void *handle, uint8_t data);
typedef void (*acia_trans_cleanup_t)(void *handle);

typedef struct
{
    acia_trans_init_t init;
    acia_trans_available_t available;
    acia_trans_read_t read;
    acia_trans_write_t write;
    acia_trans_cleanup_t cleanup;
} acia_trans_interface_t;

typedef struct acia_s *acia_t;

acia_t acia_init(sys_cxt_t system_cxt, const acia_trans_interface_t *transport, void *transport_params);
void acia_write(acia_t handle, uint8_t reg, uint8_t val);
uint8_t acia_read(acia_t handle, uint8_t reg);
void acia_cleanup(acia_t handle);

#endif
