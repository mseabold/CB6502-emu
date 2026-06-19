#include <stdbool.h>
#include <string.h>
#include <unity/unity.h>
#include <unity_internals.h>
#include "emulator.h"
#include "via.h"
#include "emu_priv_types.h"

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
    via = via_init(NULL);

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

    via = via_init(emu);

    TEST_ASSERT_NOT_NULL(via);

    TEST_ASSERT_TRUE(via_register(via, &decoder, 0x1000, false));
}

void test_callback(void)
{
    via_test_events_t events;

    events.numEvents = 0;

    via = via_init(NULL);

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
    via = via_init(NULL);

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
    via = via_init(NULL);

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

    /* Ensure the latch triggered IFR bit. */
    TEST_ASSERT_BITS_HIGH(0x82, via_read(via, 0xD));

    /* Ensure the latched value is taken correctly. */
    TEST_ASSERT_EQUAL_UINT8(0x55, via_read(via, 1));

    /* Ensure read back the latched value cleared the IFR bit. */
    TEST_ASSERT_BITS_LOW(0x02, via_read(via, 0xD));

    /* Latch and check VIA_PORTB */
    via_write_ctrl(via, VIA_CB1, true);

    /* Ensure the latch triggered IFR bit. */
    TEST_ASSERT_BITS_HIGH(0x90, via_read(via, 0xD));

    /* Ensure the latched value is taken correctly. */
    TEST_ASSERT_EQUAL_UINT8(0x55, via_read(via, 0));

    /* Ensure read back the latched value cleared the IFR bit. */
    TEST_ASSERT_BITS_LOW(0x10, via_read(via, 0xD));
}

void test_ier(void)
{
    via = via_init(NULL);
    TEST_ASSERT_NOT_NULL(via);

    TEST_ASSERT_EQUAL_UINT8(0, via_read(via, 0xe));
    via_write(via, 0xe, 0x91);
    TEST_ASSERT_EQUAL_UINT8(0x11, via_read(via, 0xe));
    via_write(via, 0xe, 0xff);
    TEST_ASSERT_EQUAL_UINT8(0x7f, via_read(via, 0xe));
    via_write(via, 0xe, 0x11);
    TEST_ASSERT_EQUAL_UINT8(0x6e, via_read(via, 0xe));
    via_write(via, 0xe, 0x7f);
    TEST_ASSERT_EQUAL_UINT8(0x0, via_read(via, 0xe));
}

void test_irq_signal(void)
{
    bus_decode_params_t decoder;

    emu = emu_init(&config);

    TEST_ASSERT_NOT_NULL(emu);

    decoder.type = BUSDECODE_RANGE;
    decoder.value.range.addr_start = 0x1000;
    decoder.value.range.addr_end = 0x100F;

    via = via_init(emu);

    TEST_ASSERT_NOT_NULL(via);

    TEST_ASSERT_TRUE(via_register(via, &decoder, 0x1000, true));

    /* Setup latching on rising edge for each port. */
    via_write(via, 0x0C, 0x11);
    via_write(via, 0x0B, 0x3);

    /* Trigger a latch. */
    via_write_ctrl(via, VIA_CA1, true);

    /* Check that the IRQ signal is now voted on the bus. */
    TEST_ASSERT_GREATER_THAN(0, emu->bus.sigvotes.irq);

    /* Read the port to clear the IFR. */
    via_read(via, 1);

    /* Test the IRQ vote was cleared on IFR clear. */
    TEST_ASSERT_EQUAL(0, emu->bus.sigvotes.irq);
}

void test_ca2_output(void)
{
    via_test_events_t events;
    via_cb_handle_t cb;

    memset(&events, 0, sizeof(events));

    emu = emu_init(&config);

    TEST_ASSERT_NOT_NULL(emu);

    via = via_init(emu);

    TEST_ASSERT_NOT_NULL(via);

    cb = via_register_callback(via, via_record_evt_cb, &events);

    TEST_ASSERT_NOT_NULL(cb);

    /* Make sure state as input pins starts low. */
    via_write_ctrl(via, VIA_CA2, false);

    /* Make sure in default configuration pins read back input state. */
    TEST_ASSERT_FALSE(via_read_ctrl(via, VIA_CA2));

    /* Write the pin state as force output high. */
    via_write(via, 0xC, 0xE);

    /* Make sure we got a callback that the pin state changed. */
    TEST_ASSERT_EQUAL_UINT(1, events.numEvents);
    TEST_ASSERT_EQUAL(VIA_EV_PORT_CHANGE, events.events[0].type);
    TEST_ASSERT_EQUAL(VIA_CTRL, events.events[0].data.port);

    /* Make sure the pin now reads back as output. */
    TEST_ASSERT(via_read_ctrl(via, VIA_CA2));

    /* Write the pin state as force output low. */
    via_write(via, 0xC, 0xC);

    /* Make sure we got a callback that the pin state changed. */
    TEST_ASSERT_EQUAL_UINT(2, events.numEvents);
    TEST_ASSERT_EQUAL(VIA_EV_PORT_CHANGE, events.events[1].type);
    TEST_ASSERT_EQUAL(VIA_CTRL, events.events[1].data.port);

    /* Make sure the pin now reads back as output. */
    TEST_ASSERT_FALSE(via_read_ctrl(via, VIA_CA2));
}

void test_cb2_output(void)
{
    via_test_events_t events;
    via_cb_handle_t cb;

    memset(&events, 0, sizeof(events));

    emu = emu_init(&config);

    TEST_ASSERT_NOT_NULL(emu);

    via = via_init(emu);

    TEST_ASSERT_NOT_NULL(via);

    cb = via_register_callback(via, via_record_evt_cb, &events);

    TEST_ASSERT_NOT_NULL(cb);

    /* Make sure state as input pins starts low. */
    via_write_ctrl(via, VIA_CB2, false);

    /* Make sure in default configuration pins read back input state. */
    TEST_ASSERT_FALSE(via_read_ctrl(via, VIA_CB2));

    /* Write the pin state as force output high. */
    via_write(via, 0xC, 0xE0);

    /* Make sure we got a callback that the pin state changed. */
    TEST_ASSERT_EQUAL_UINT(1, events.numEvents);
    TEST_ASSERT_EQUAL(VIA_EV_PORT_CHANGE, events.events[0].type);
    TEST_ASSERT_EQUAL(VIA_CTRL, events.events[0].data.port);

    /* Make sure the pin now reads back as output. */
    TEST_ASSERT(via_read_ctrl(via, VIA_CB2));

    /* Write the pin state as force output low. */
    via_write(via, 0xC, 0xC0);

    /* Make sure we got a callback that the pin state changed. */
    TEST_ASSERT_EQUAL_UINT(2, events.numEvents);
    TEST_ASSERT_EQUAL(VIA_EV_PORT_CHANGE, events.events[1].type);
    TEST_ASSERT_EQUAL(VIA_CTRL, events.events[1].data.port);

    /* Make sure the pin now reads back as output. */
    TEST_ASSERT_FALSE(via_read_ctrl(via, VIA_CB2));
}

void test_porta_read_hs(void)
{
    via_test_events_t events;

    memset(&events, 0, sizeof(events));

    emu = emu_init(&config);
    TEST_ASSERT_NOT_NULL(emu);

    via = via_init(emu);
    TEST_ASSERT_NOT_NULL(via);

    TEST_ASSERT_NOT_NULL(via_register_callback(via, via_record_evt_cb, &events));

    /* Drive CA1 is that is default inactive state. */
    via_write_ctrl(via, VIA_CA1, true);

    /* Put CA2 in Handshake Output. */
    via_write(via, 0xC, 0x8);

    /* CA2 / Data Ready should be high. */
    TEST_ASSERT(via_read_ctrl(via, VIA_CA2));

    /* Read PORTA, which should trigger Data Taken. */
    via_read(via, 0x01);

    /* Make sure we got a callback that the pin state changed. Note that
     * writing PCR triggers the callback as CA2 goes high for idle, so
     * there should now be *2* events. */
    TEST_ASSERT_EQUAL_UINT(2, events.numEvents);
    TEST_ASSERT_EQUAL(VIA_EV_PORT_CHANGE, events.events[1].type);
    TEST_ASSERT_EQUAL(VIA_CTRL, events.events[1].data.port);

    /* Make sure the pin now reads back as output low. */
    TEST_ASSERT_FALSE(via_read_ctrl(via, VIA_CA2));

    /* Now trigger current CA1 active edge (negative) as Data Taken. */
    via_write_ctrl(via, VIA_CA1, false);

    /* IFR should have been triggered now and Data Ready should have gone back high. */
    TEST_ASSERT_BIT_HIGH(1, via_read(via, 0xD));

    TEST_ASSERT_EQUAL_UINT(3, events.numEvents);
    TEST_ASSERT_EQUAL(VIA_EV_PORT_CHANGE, events.events[2].type);
    TEST_ASSERT_EQUAL(VIA_CTRL, events.events[2].data.port);

    /* Make sure the pin now reads back as output high. */
    TEST_ASSERT(via_read_ctrl(via, VIA_CA2));
}

void test_porta_read_pulse(void)
{
    via_test_events_t events;

    memset(&events, 0, sizeof(events));

    emu = emu_init(&config);
    TEST_ASSERT_NOT_NULL(emu);

    via = via_init(emu);
    TEST_ASSERT_NOT_NULL(via);

    TEST_ASSERT_NOT_NULL(via_register_callback(via, via_record_evt_cb, &events));

    /* Drive CA1 is that is default inactive state. */
    via_write_ctrl(via, VIA_CA1, true);

    /* Put CA2 in Handshake Pulse. */
    via_write(via, 0xC, 0xA);

    /* CA2 / Data Ready should be high. */
    TEST_ASSERT(via_read_ctrl(via, VIA_CA2));

    /* Read PORTA, which should trigger Data Taken. */
    via_read(via, 0x01);

    /* Make sure we got a callback that the pin state changed. Note that
     * writing PCR triggers the callback as CA2 goes high for idle, so
     * there should now be *2* events. */
    TEST_ASSERT_EQUAL_UINT(2, events.numEvents);
    TEST_ASSERT_EQUAL(VIA_EV_PORT_CHANGE, events.events[1].type);
    TEST_ASSERT_EQUAL(VIA_CTRL, events.events[1].data.port);

    /* Make sure the pin now reads back as output low. */
    TEST_ASSERT_FALSE(via_read_ctrl(via, VIA_CA2));

    /* Tick the main clock once. */
    emu_tick(emu);

    /* Clock single should have reset the pulse. */
    TEST_ASSERT_EQUAL_UINT(3, events.numEvents);
    TEST_ASSERT_EQUAL(VIA_EV_PORT_CHANGE, events.events[2].type);
    TEST_ASSERT_EQUAL(VIA_CTRL, events.events[2].data.port);

    /* Make sure the pin now reads back as output high. */
    TEST_ASSERT(via_read_ctrl(via, VIA_CA2));
}

static void test_write_hs(bool porta)
{
    via_test_events_t events;
    via_ctrl_pin_t c1_pin = porta ? VIA_CA1 : VIA_CB1;
    via_ctrl_pin_t c2_pin = porta ? VIA_CA2 : VIA_CB2;
    uint8_t pcr_val = porta ? 0x08 : 0x80;
    uint8_t ifr_bit = porta ? 1 : 4;
    uint8_t port_reg = porta ? 0x1 : 0x0;

    memset(&events, 0, sizeof(events));

    emu = emu_init(&config);
    TEST_ASSERT_NOT_NULL(emu);

    via = via_init(emu);
    TEST_ASSERT_NOT_NULL(via);

    TEST_ASSERT_NOT_NULL(via_register_callback(via, via_record_evt_cb, &events));

    /* Drive C1 is that is default inactive state. */
    via_write_ctrl(via, c1_pin, true);

    /* Put C2 in Handshake Output. */
    via_write(via, 0xC, pcr_val);

    /* Put events back to 0 for clarity. */
    events.numEvents = 0;

    /* C2 / Data Ready should be high. */
    TEST_ASSERT(via_read_ctrl(via, c2_pin));

    /* Write PORTA, which should start the handsake trigger. */
    /* NOTE: We are not configured DDR (See VIA note HW7) */
    via_write(via, port_reg, 0);

    /* Handshake should not occur until rising edge of next main clock. */
    TEST_ASSERT_EQUAL_UINT(0, events.numEvents);

    /* Tick the clock
     * TODO: Perhaps this should force only the rising edge phase. */
    emu_tick(emu);

    /* Make sure we got a callback that the pin state changed now that
     * the rising edge has triggered the handsake. */
    TEST_ASSERT_EQUAL_UINT(1, events.numEvents);
    TEST_ASSERT_EQUAL(VIA_EV_PORT_CHANGE, events.events[0].type);
    TEST_ASSERT_EQUAL(VIA_CTRL, events.events[0].data.port);

    /* Make sure the pin now reads back as output low. */
    TEST_ASSERT_FALSE(via_read_ctrl(via, c2_pin));

    /* Now trigger current CA1 active edge (negative) as Data Taken. */
    via_write_ctrl(via, c1_pin, false);

    /* IFR should have been triggered now and Data Ready should have gone back high. */
    TEST_ASSERT_BIT_HIGH(ifr_bit, via_read(via, 0xD));

    TEST_ASSERT_EQUAL_UINT(2, events.numEvents);
    TEST_ASSERT_EQUAL(VIA_EV_PORT_CHANGE, events.events[1].type);
    TEST_ASSERT_EQUAL(VIA_CTRL, events.events[1].data.port);

    /* Make sure the pin now reads back as output high. */
    TEST_ASSERT(via_read_ctrl(via, c2_pin));
}

static void test_write_hs_pulse(bool porta)
{
    via_test_events_t events;
    via_ctrl_pin_t c1_pin = porta ? VIA_CA1 : VIA_CB1;
    via_ctrl_pin_t c2_pin = porta ? VIA_CA2 : VIA_CB2;
    uint8_t pcr_val = porta ? 0x0A : 0xA0;
    uint8_t port_reg = porta ? 0x1 : 0x0;

    memset(&events, 0, sizeof(events));

    emu = emu_init(&config);
    TEST_ASSERT_NOT_NULL(emu);

    via = via_init(emu);
    TEST_ASSERT_NOT_NULL(via);

    TEST_ASSERT_NOT_NULL(via_register_callback(via, via_record_evt_cb, &events));

    /* Drive C1 is that is default inactive state. */
    via_write_ctrl(via, c1_pin, true);

    /* Put C2 in Handshake Output. */
    via_write(via, 0xC, pcr_val);

    /* Put events back to 0 for clarity. */
    events.numEvents = 0;

    /* C2 / Data Ready should be high. */
    TEST_ASSERT(via_read_ctrl(via, c2_pin));

    /* Write PORTA, which should start the handsake trigger. */
    /* NOTE: We are not configured DDR (See VIA note HW7) */
    via_write(via, port_reg, 0);

    /* Handshake should not occur until rising edge of next main clock. */
    TEST_ASSERT_EQUAL_UINT(0, events.numEvents);

    /* Tick the clock
     * TODO: Perhaps this should force only the rising edge phase. */
    emu_tick(emu);

    /* Make sure we got a callback that the pin state changed now that
     * the rising edge has triggered the handsake. */
    TEST_ASSERT_EQUAL_UINT(1, events.numEvents);
    TEST_ASSERT_EQUAL(VIA_EV_PORT_CHANGE, events.events[0].type);
    TEST_ASSERT_EQUAL(VIA_CTRL, events.events[0].data.port);

    /* Make sure the pin now reads back as output low. */
    TEST_ASSERT_FALSE(via_read_ctrl(via, c2_pin));

    /* Tick again to clear the pulse */
    emu_tick(emu);

    TEST_ASSERT_EQUAL_UINT(2, events.numEvents);
    TEST_ASSERT_EQUAL(VIA_EV_PORT_CHANGE, events.events[1].type);
    TEST_ASSERT_EQUAL(VIA_CTRL, events.events[1].data.port);

    /* Make sure the pin now reads back as output high. */
    TEST_ASSERT(via_read_ctrl(via, c2_pin));
}

void test_porta_write_hs(void)
{
    test_write_hs(true);
}

void test_portb_write_hs(void)
{
    test_write_hs(false);
}

void test_porta_write_pulse(void)
{
    test_write_hs_pulse(true);
}

void test_portb_write_pulse(void)
{
    test_write_hs_pulse(true);
}

void test_t1_one_shot(void)
{
    emu = emu_init(&config);
    TEST_ASSERT_NOT_NULL(emu);

    via = via_init(emu);
    TEST_ASSERT_NOT_NULL(via);

    /* Default state is one-shot, so just set up the timer. */
    via_write(via, 0x4, 5);
    via_write(via, 0x5, 0);

    /* Expiration should take 5+1.5 cycles. Tick 6 times and make sure it hasn't
     * expired. */
    emu_tick(emu);
    emu_tick(emu);
    emu_tick(emu);
    emu_tick(emu);
    emu_tick(emu);
    emu_tick(emu);

    TEST_ASSERT_BIT_LOW(6, via_read(via, 0xD));

    emu_tick(emu);

    /* Ensure the timer expired. */
    TEST_ASSERT_BIT_HIGH(6, via_read(via, 0xD));

    /* Reading back T1LL should not clear IFR6. */
    via_read(via, 0x6);
    TEST_ASSERT_BIT_HIGH(6, via_read(via, 0xD));

    via_read(via, 0x4);

    /* Ensure reading back T1CL clears IFR6. */
    TEST_ASSERT_BIT_LOW(6, via_read(via, 0xD));
}

void test_t1_continuous(void)
{
    emu = emu_init(&config);
    TEST_ASSERT_NOT_NULL(emu);

    via = via_init(emu);
    TEST_ASSERT_NOT_NULL(via);

    /* Put T1 into continuous mode. */
    via_write(via, 0xB, 0x40);

    via_write(via, 0x4, 5);
    via_write(via, 0x5, 0);

    /* Expiration should take 5+1.5 cycles. Tick 6 times and make sure it hasn't
     * expired. */
    emu_tick(emu);
    emu_tick(emu);
    emu_tick(emu);
    emu_tick(emu);
    emu_tick(emu);
    emu_tick(emu);

    TEST_ASSERT_BIT_LOW(6, via_read(via, 0xD));

    emu_tick(emu);

    /* Ensure the timer expired. */
    TEST_ASSERT_BIT_HIGH(6, via_read(via, 0xD));

    /* Reading back T1LL should not clear IFR6. */
    via_read(via, 0x6);
    TEST_ASSERT_BIT_HIGH(6, via_read(via, 0xD));

    via_read(via, 0x4);

    /* Ensure reading back T1CL clears IFR6. */
    TEST_ASSERT_BIT_LOW(6, via_read(via, 0xD));

    /* T1CL and T1CH should be loaded with latches again now. */
    TEST_ASSERT_EQUAL_UINT8(5, via_read(via, 0x4));
    TEST_ASSERT_EQUAL_UINT8(0, via_read(via, 0x5));

    /* Click 6 more times. */
    emu_tick(emu);
    emu_tick(emu);
    emu_tick(emu);
    emu_tick(emu);
    emu_tick(emu);
    emu_tick(emu);

    TEST_ASSERT_BIT_LOW(6, via_read(via, 0xD));

    emu_tick(emu);

    /* Ensure the timer expired. */
    TEST_ASSERT_BIT_HIGH(6, via_read(via, 0xD));

    /* Test that writing T1LH resets IFR6 */
    via_write(via, 0x7, 0);
    TEST_ASSERT_BIT_LOW(6, via_read(via, 0xD));
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
    RUN_TEST(test_ier);
    RUN_TEST(test_irq_signal);
    RUN_TEST(test_ca2_output);
    RUN_TEST(test_cb2_output);
    RUN_TEST(test_porta_read_hs);
    RUN_TEST(test_porta_read_pulse);
    RUN_TEST(test_porta_write_hs);
    RUN_TEST(test_portb_write_hs);
    RUN_TEST(test_porta_write_pulse);
    RUN_TEST(test_portb_write_pulse);
    RUN_TEST(test_t1_one_shot);
    RUN_TEST(test_t1_continuous);

    return UNITY_END();
}
