#ifndef __BUS_PRIV_TYPES_H__
#define __BUS_PRIV_TYPES_H__

#include <stdint.h>
#include "bus.h"
#include "util.h"

typedef enum
{
    SV_NMI_EDGE_PENDING = 0x01, /**< Indicates an NMI edge has occured that has not been processed by the cpu core. */
} bus_sv_flags_t;

typedef struct
{
    uint16_t addr;
    uint8_t val;
    bool write;
    bus_flags_t flags;
} bus_op_t;

/** Signal voting information */
typedef struct
{
    uint32_t allocated; /**< Tracks the allocated voters */
    uint32_t irq;       /**< Tracks the votes per-voter for IRQ */
    uint32_t nmi;       /**< Tracks the votes per-voter for NMI */
    uint32_t rdy;       /**< Tracks the votes per-voter for RDY */
    uint32_t be;        /**< Tracks the votes per-voter for BE */
    bus_sv_flags_t flags; /**< Additional boolean flags for the votes context. */
} bus_sigvotes_t;

/** Internal Bus context structure */
typedef struct bus_s
{
    bool init;              /**< Indicates if the bus context has been initialized. */
    listnode_t connlist;    /**< List of registered bus connections. */
    listnode_t tracelist;   /**< List of regisrered trace callbacks. */
    bus_sigvotes_t sigvotes; /**< Information regarding external signal voting. */
    bus_op_t lastop; /**< Tracks the list bus operation performed. */
} bus_t;

#endif /* end of include guard: __BUS_PRIV_TYPES_H__ */
