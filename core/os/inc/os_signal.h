#ifndef __OS_SIGNAL_H__
#define __OS_SIGNAL_H__

#include <stddef.h>

/**
 * Defines the types of system signals that can be registered.
 */
typedef enum
{
    OS_CTRLC, /**< Ctrl-C OS Signal. */
} os_signal_t;

/**
 * Handle type for a registered signal callback.
 */
typedef void *os_sigcb_handle_t;

/**
 * Callback prototype for processing OS signals.
 *
 * @param[in] signal The OS signal that is being processed
 * @param[in] userdata User-supplied parameter when the callback was registered
 */
typedef void (*os_sigcb_t)(os_signal_t signal, void *userdata);

/**
 * Invalid OS Signal handle
 */
#define OS_INVALID_HANDLE NULL

/**
 * Register a callback to process the given signal
 *
 * @param[in] signal The signal to register a callback for
 * @param[in] cb     The callback function to register
 * @param[in] userdata User parameter that will be passed back when the callback is called
 *
 * @return A handle for the registered callback that can be used to unregister or OS_INVALID_HANDLE
 *         if unable to register
 */
os_sigcb_handle_t os_register_signal(os_signal_t signal, os_sigcb_t cb, void *userdata);

/**
 * Unregisters a previously registered OS signal callback
 *
 * @param[in] handle The handle the was returned by a previousl call to os_register_signal()
 */
void os_unregister_signal(os_sigcb_handle_t handle);

#endif /* end of include guard: __OS_SIGNAL_H__ */
