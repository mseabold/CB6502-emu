#ifndef __ACIA_UNIX_SOCK_H__
#define __ACIA_UNIX_SOCK_H__

#include "acia.h"

#define ACIA_DEFAULT_UNIX_SOCKNAME "acia.sock"

typedef struct
{
    const char *sockname;
} acia_unix_sock_params_t;

const acia_trans_interface_t *acia_unix_get_iface(void);

#endif /* end of include guard: __ACIA_UNIX_SOCK_H__ */
