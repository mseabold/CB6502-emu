#ifndef __VIA_H__
#define __VIA_H__

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    VIA_PORTA,
    VIA_PORTB,
    VIA_CTRL
} via_port_data_t;

#define VIA_CTRL_CA1 0x01
#define VIA_CTRL_CA2 0x02
#define VIA_CTRL_CB1 0x04
#define VIA_CTRL_CB2 0x08
/**
 * VIA protocol write handler
 *
 * @param[in] data_type The type of data being written
 * @param[in] data      The data being written
 * @
 */
typedef void (*prot_put_t)(via_port_data_t data_type, uint8_t data, void *userdata);

/**
 * VIA protocol read handler. The callee should only update bits of the
 * supplied field which it is currently driving. Multiple registered
 * protocols attempting to drive the same bits is currently undefined behavior.
 * The VIA will mask of in PORT bits that are configured as OUTPUT in the
 * corresponding DDR regardless of what the protocol attempts to drive the
 * bit to. Protocols trying to drive bits that are configured as OUTPUT will
 * be ignored.
 *
 * @param[in] port  The of data being queried
 * @param[out] data Pointer to data which the protocol should update with its driven bits
 */
typedef void (*prot_get_t)(via_port_data_t data_type, uint8_t *data, void *userdata);

typedef struct via_s *via_t;

typedef struct via_protocol_s
{
    prot_put_t put;
    prot_get_t get;
} via_protocol_t;

via_t via_init(void);
void via_cleanup(via_t handle);
void via_write(via_t handle, uint8_t reg, uint8_t val);
uint8_t via_read(via_t handle, uint8_t reg);
bool via_register_protocol(via_t handle, const via_protocol_t *protocol, void *userdata);
void via_unregister_protocol(via_t handle, const via_protocol_t *protocol);

#endif /* __VIA_H__ */
