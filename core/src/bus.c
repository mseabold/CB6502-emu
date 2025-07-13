#include <stdlib.h>

#include "emu_priv_types.h"
#include "bus_api.h"
#include "log.h"

typedef struct bus_conn_s
{
    listnode_t list;
    bus_decode_params_t params;
    bus_handlers_t handlers;
    void *userdata;
} bus_conn_t;

typedef struct bus_tracer_s
{
    listnode_t list;
    bus_trace_cb_t callback;
    void *userdata;
} bus_tracer_t;

static bool bus_match_addr(bus_decode_params_t *params, uint16_t addr, bool write, void *userdata)
{
    bool match;

    switch(params->type)
    {
        case BUSDECODE_RANGE:
            match = (addr >= params->value.range.addr_start) && (addr <= params->value.range.addr_end);
            break;
        case BUSDECODE_MASK:
            match = ((addr & params->value.mask.addr_mask) == params->value.mask.addr_value);
            break;
        case BUSDECODE_CUSTOM:
            match = params->value.custom(addr, write, userdata);
            break;
        default:
            match = false;
            break;
    }

    return match;
}

static bool bus_validate_params(const bus_decode_params_t *params)
{
    bool valid;

    switch(params->type)
    {
        case BUSDECODE_RANGE:
            valid = params->value.range.addr_end >= params->value.range.addr_start;
            break;
        case BUSDECODE_MASK:
            valid = params->value.mask.addr_mask != 0;
            break;
        case BUSDECODE_CUSTOM:
            valid = true;
            break;
        default:
            valid = false;
            break;
    }

    return valid;
}

bool bus_init(bus_t *bus)
{
    bool ret = true;

    if(bus != NULL)
    {
        list_init(&bus->connlist);
        list_init(&bus->tracelist);
    }
    else
    {
        ret = false;
    }

    return ret;
}

void bus_cleanup(bus_t *bus)
{
    bus_conn_t *conn;
    bus_tracer_t *tracer;
    listnode_t *tail;

    while(!list_empty(&bus->connlist))
    {
        tail = list_tail(&bus->connlist);

        list_remove(tail);

        conn = list_container(tail, bus_conn_t, list);

        free(conn);
    }

    while(!list_empty(&bus->tracelist))
    {
        tail = list_tail(&bus->tracelist);

        list_remove(tail);

        tracer = list_container(tail, bus_tracer_t, list);

        free(tracer);
    }
}

static uint8_t bus_read_peek_i(bus_t *bus, uint16_t addr, bool peek)
{
    uint8_t ret;
    uint8_t conn_read_val;
    bool matched = false;
    listnode_t *cur;
    bus_conn_t *conn;
    bus_tracer_t *tracer;
    bus_read_cb_t cb;

    list_iterate(&bus->connlist, cur)
    {
        conn = list_container(cur, bus_conn_t, list);

        if(bus_match_addr(&conn->params, addr, true, conn->userdata))
        {
            cb = peek ? conn->handlers.peek : conn->handlers.read;

            if(cb)
            {
                conn_read_val = cb(addr, conn->userdata);

                if((matched) && (conn_read_val != ret))
                {
                    log_print(lWARNING, "Multiple bus connections driving data for Address: 0x%04x. This could damage actual HW.", addr);
                }
                else
                {
                    matched = true;
                    ret = conn_read_val;
                }
            }
        }
    }

    if(!matched)
    {
        ret = 0xFF;
    }

    /* only trace on actual bus transactions */
    if(!peek)
    {
        list_iterate(&bus->tracelist, cur)
        {
            tracer = list_container(cur, bus_tracer_t, list);

            tracer->callback(addr, ret, false, tracer->userdata);
        }
    }

    return ret;
}

uint8_t bus_read(bus_t *bus, uint16_t addr)
{
    return bus_read_peek_i(bus, addr, false);
}

uint8_t bus_peek(bus_t *bus, uint16_t addr)
{
    return bus_read_peek_i(bus, addr, true);
}

void bus_write(bus_t *bus, uint16_t addr, uint8_t value)
{
    listnode_t *cur;
    bus_conn_t *conn;
    bus_tracer_t *tracer;
    bus_read_cb_t cb;

    list_iterate(&bus->connlist, cur)
    {
        conn = list_container(cur, bus_conn_t, list);

        if(bus_match_addr(&conn->params, addr, false, conn->userdata))
        {
            if(conn->handlers.write)
            {
                conn->handlers.write(addr, value, conn->userdata);
            }
        }
    }

    list_iterate(&bus->tracelist, cur)
    {
        tracer = list_container(cur, bus_tracer_t, list);

        tracer->callback(addr, value, true, tracer->userdata);
    }
}

bus_cb_handle_t emu_bus_register(cbemu_t emu, const bus_decode_params_t *params, const bus_handlers_t *handlers, void *userdata)
{
    bus_conn_t *conn;

    if(emu == NULL || params == NULL || handlers == NULL || !bus_validate_params(params))
    {
        return NULL;
    }

    conn = malloc(sizeof(bus_conn_t));

    if(conn != NULL)
    {
        conn->params = *params;
        conn->handlers = *handlers;
        conn->userdata = userdata;

        list_add_tail(&emu->bus.connlist, &conn->list);
    }

    return conn;
}

void emu_bus_unregister(cbemu_t emu, bus_cb_handle_t handle)
{
    bus_conn_t *conn = (bus_conn_t *)handle;

    if(conn != NULL)
    {
        /* TODO For safety, should maybe call list_contains. */
        list_remove(&conn->list);

        free(conn);
    }
}

bus_cb_handle_t emu_bus_add_tracer(cbemu_t emu, const bus_trace_cb_t callback, void *userdata)
{
    bus_tracer_t *tracer;

    if(emu == NULL || callback == NULL)
    {
        return NULL;
    }

    tracer = malloc(sizeof(bus_tracer_t));

    if(tracer != NULL)
    {
        tracer->callback = callback;
        tracer->userdata = userdata;

        list_add_tail(&emu->bus.tracelist, &tracer->list);
    }

    return tracer;
}

void emu_bus_remove_tracer(cbemu_t emu, bus_cb_handle_t handle)
{
    bus_tracer_t *tracer = (bus_tracer_t *)handle;

    if(tracer != NULL)
    {
        list_remove(&tracer->list);

        free(tracer);
    }
}
