#ifndef __OS_SIGNAL_H__
#define __OS_SIGNAL_H__

#include <stddef.h>

typedef enum
{
    OS_CTRLC
} os_signal_t;

typedef void *os_sigcb_handle_t;
typedef void (*os_sigcb_t)(os_signal_t signal, void *userdata);
#define OS_INVALID_HANDLE NULL

os_sigcb_handle_t os_register_signal(os_signal_t signal, os_sigcb_t cb, void *userdata);
void os_unregister_signal(os_sigcb_handle_t handle);

#endif /* end of include guard: __OS_SIGNAL_H__ */
