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
    bool is_base_mask; /**< Indicates with base is a mask or an address. */

    /**
     * Base address/mask of the IO module. This allows the IO module instance to derive
     * an internal offset/address based on the address being read/written.
     *
     * If is_base_mask is true, then incoming bus address is bitwise-anded with
     * this value to derive the internal address.
     *
     * If is_base_Mask is false, then this value is subtracted from the incoming
     * bus address to derive the internal address.
     */
    uint16_t base;
} io_bus_params_t;

#endif /* __IOUTIL_H__ */
