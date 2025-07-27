#ifndef __BUS_PRIV_TYPES_H__
#define __BUS_PRIV_TYPES_H__

#include <stdint.h>
#include "util.h"

/** Internal Bus context structure */
typedef struct bus_s
{
    bool init;              /**< Indicates if the bus context has been initialized. */
    listnode_t connlist;    /**< List of registered bus connections. */
    listnode_t tracelist;   /**< List of regisrered trace callbacks. */
} bus_t;

#endif /* end of include guard: __BUS_PRIV_TYPES_H__ */
