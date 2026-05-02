#include <stdbool.h>
#include <string.h>
#include <unity/unity.h>
#include <unity_internals.h>
#include "emulator.h"
#include "via.h"

static cbemu_t emu;
static via_t via;
static via_cb_handle_t cb;
static const emu_config_t config = { CLOCK_FREQ, 1000000 };

#define MAX_VIA_TEST_EVENTS 8

typedef struct
{
    via_event_data_t events[MAX_VIA_TEST_EVENTS];
    unsigned int numEvents;
} via_test_events_t;


static void via_record_evt_cb(via_t via, const via_event_data_t *event, void *userdata)
{
    via_test_events_t *events = (via_test_events_t *)userdata;

    if(events->numEvents < MAX_VIA_TEST_EVENTS)
    {
        memcpy(&events->events[events->numEvents], event, sizeof(via_event_data_t));
        events->numEvents++;
    }
}

void test_init_direct(void)
{
    via = via_init();

    TEST_ASSERT_NOT_NULL(via);
}

void test_init_bus(void)
{
    bus_decode_params_t decoder;

    emu = emu_init(&config);

    TEST_ASSERT_NOT_NULL(emu);

    decoder.type = BUSDECODE_RANGE;
    decoder.value.range.addr_start = 0x1000;
    decoder.value.range.addr_end = 0x100F;

    via = via_init();

    TEST_ASSERT_NOT_NULL(via);

    TEST_ASSERT_TRUE(via_register(via, emu, &decoder, 0x1000, false));
}

void test_callback(void)
{
    via_test_events_t events;

    events.numEvents = 0;

    via = via_init();

    TEST_ASSERT_NOT_NULL(via);

    cb = via_register_callback(via, via_record_evt_cb, &events);

    TEST_ASSERT_NOT_NULL(cb);

    via_write(via, 0x03, 0xAA);

    TEST_ASSERT_EQUAL_INT(1, events.numEvents);
    TEST_ASSERT_EQUAL_INT(VIA_PORTA, events.events[0].data.port);

    via_write(via, 0x02, 0xAA);

    TEST_ASSERT_EQUAL_INT(2, events.numEvents);
    TEST_ASSERT_EQUAL_INT(VIA_PORTB, events.events[1].data.port);

    via_unregister_callback(via, cb);
    cb = NULL;

    via_write(via, 0x03, 0x55);

    TEST_ASSERT_EQUAL_INT(2, events.numEvents);
}

void test_port_data(void)
{
    via = via_init();

    TEST_ASSERT_NOT_NULL(via);

    /* All pins should start as input. */
    via_write_data_port(via, true, 0xff, 0xff);
    TEST_ASSERT_EQUAL_UINT8(0xff, via_read(via, 1));

    /* Test PORTB */
    via_write_data_port(via, false, 0xff, 0xff);
    TEST_ASSERT_EQUAL_UINT8(0xff, via_read(via, 0));

    /* Mix input and output directions. Output pins should read back 0/output reg */
    via_write(via, 3, 0xaa);
    TEST_ASSERT_EQUAL_UINT8(0x55, via_read_data_port(via, true));
    via_write(via, 2, 0xaa);
    TEST_ASSERT_EQUAL_UINT8(0x55, via_read_data_port(via, false));

    /* Write some output data. */
    via_write(via, 0, 0xa0);
    via_write(via, 1, 0xa0);
    TEST_ASSERT_EQUAL_UINT8(0xf5, via_read_data_port(via, true));
    TEST_ASSERT_EQUAL_UINT8(0xf5, via_read_data_port(via, false));

    /* Write a mask of input bits. */
    via_write_data_port(via, true, 0x0f, 0);
    via_write_data_port(via, false, 0x0f, 0);

    /* Reads should be new masked inputs + output regs. */
    TEST_ASSERT_EQUAL_UINT8(0xf0, via_read(via, 0));
    TEST_ASSERT_EQUAL_UINT8(0xf0, via_read(via, 1));
}

void test_port_latching(void)
{
    via = via_init();

    TEST_ASSERT_NOT_NULL(via);

    /* Start within only some inputs. */
    via_write(via, 2, 0xaa);
    via_write(via, 3, 0xaa);

    /* Write initial input data. */
    via_write_data_port(via, true, 0xff, 0x0);
    via_write_data_port(via, false, 0xff, 0x0);

    /* Make sure inputs currently match. */
    TEST_ASSERT_EQUAL_UINT8(0x0, via_read(via, 0));
    TEST_ASSERT_EQUAL_UINT8(0x0, via_read(via, 1));

    /* Setup latching on rising edge for each port. */
    via_write(via, 0x0C, 0x11);
    via_write(via, 0x0B, 0x3);

    /* Flip the inputs. */
    via_write_data_port(via, true, 0xff, 0xff);
    via_write_data_port(via, false, 0xff, 0xff);

    /* Pins should not have changed yet. */
    TEST_ASSERT_EQUAL_UINT8(0x0, via_read(via, 0));
    TEST_ASSERT_EQUAL_UINT8(0x0, via_read(via, 1));

    /* Latch and check PORTA */
    via_write_ctrl(via, VIA_CA1, true);
    TEST_ASSERT_EQUAL_UINT8(0x55, via_read(via, 1));

    /* Latch and check VIA_PORTB */
    via_write_ctrl(via, VIA_CB1, true);
    TEST_ASSERT_EQUAL_UINT8(0x55, via_read(via, 0));
}

void setUp(void)
{
}

void tearDown(void)
{
    if(via != NULL)
    {
        via_cleanup(via);
        via = NULL;
        cb = NULL;
    }

    if(emu != NULL)
    {
        emu_cleanup(emu);
        emu = NULL;
    }
}
int main(int argc, char **argv)
{
    UNITY_BEGIN();

    RUN_TEST(test_init_direct);
    RUN_TEST(test_init_bus);
    RUN_TEST(test_callback);
    RUN_TEST(test_port_data);
    RUN_TEST(test_port_latching);

    return UNITY_END();
}
