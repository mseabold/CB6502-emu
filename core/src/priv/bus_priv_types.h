#ifndef __BUS_PRIV_TYPES_H__
#define __BUS_PRIV_TYPES_H__

#include <stdint.h>
#include "util.h"

typedef struct bus_s
{
    listnode_t connlist;
    listnode_t tracelist;
} bus_t;

#endif /* end of include guard: __BUS_PRIV_TYPES_H__ */
