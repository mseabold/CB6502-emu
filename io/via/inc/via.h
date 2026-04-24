#ifndef __VIA_H__
#define __VIA_H__

#include "emulator.h"

typedef enum {
    VIA_PORTA,
    VIA_PORTB,
    VIA_CTRL
} via_port_data_t;

typedef enum {
    VIA_CA1,
    VIA_CA2,
    VIA_CB1,
    VIA_CB2
} via_ctrl_pin_t;

/**
 * Defines the types of events that may be dispatched by a VIA module.
 */
typedef enum
{
    VIA_EV_PORT_CHANGE /**< Indicates the output data on one of the VIA ports has changed. */
} via_event_type_t;

/**
 * Contains the data dispatched in a VIA event
 */
typedef struct
{
    /** The type of event being dispatched. */
    via_event_type_t type;

    /** The data associated with the event. */
    union
    {
        via_port_data_t port; /**< Valid when type indicates VIA_EV_PORT_CHANGE. Indicates which port has changed. */
    } data;
} via_event_data_t;

typedef struct via_s *via_t;
typedef void *via_cb_handle_t;

#define VIA_CTRL_CA1 0x01
#define VIA_CTRL_CA2 0x02
#define VIA_CTRL_CB1 0x04
#define VIA_CTRL_CB2 0x08

/**
 * Prototype for a VIA event handler callback that can be registered with a VIA module.
 *
 * @param[in] via       The associated VIA with this event.
 * @param[in] event     The type and associated data for this event.
 * @param[in] userdata  Parameter given when event callback is regstered.
 */
typedef void (*via_event_callback_t)(via_t via, const via_event_data_t *event, void *userdata);

via_t via_init(void);
void via_cleanup(via_t handle);
bool via_register(via_t handle, const cbemu_t emu, const bus_decode_params_t *decoder, uint16_t base);
void via_write(via_t handle, uint8_t reg, uint8_t val);
uint8_t via_read(via_t handle, uint8_t reg);

via_cb_handle_t via_register_callback(via_t handle, via_event_callback_t callback, void *userdata);
void via_unregister_callback(via_t handle, via_cb_handle_t cb_handle);
void via_write_data_port(via_t handle, bool porta, uint8_t mask, uint8_t data);
void via_write_ctrl(via_t handle, via_ctrl_pin_t pin, bool val);
uint8_t via_read_data_port(via_t handle, bool porta);
bool via_read_ctrl(via_t handle, via_ctrl_pin_t pin);

#endif /* __VIA_H__ */
