#ifndef __IOUTIL_H__
#define __IOUTIL_H__

#include "emulator.h"

/**
 * Defines parameters that allow the IO  modules
 * to register themselves with the emulator bus.
 */
typedef struct
{
    cbemu_t emulator; /**< Emulator handle to register with. */
    const bus_decode_params_t *decoder; /**< Bus address parameters for decoding. */
    /**
     * Base address of the IO module. This allows the IO module instance to derive
     * an internal offset/address by subtracting this bus from the address supplied
     * by the bus operation. If a simple subtraction from a base is not suitable
     * for a more complex bus decoding scheme, then external decoding should be used
     * and the IO should be interacted with directly.
     */
    uint16_t base;
} io_bus_params_t;

#endif /* __IOUTIL_H__ */
