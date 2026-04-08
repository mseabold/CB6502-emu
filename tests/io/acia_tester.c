#include <unity/unity.h>
#include <string.h>

#include "emulator.h"
#include "acia.h"

#define MAX_TEST_WRITE_BYTES 16

typedef struct
{
    bool avail;
    unsigned int write_cnt;
    unsigned int read_cnt;
    uint8_t read_byte;
    uint8_t write_bytes[MAX_TEST_WRITE_BYTES];
} acia_test_data_t;

static cbemu_t emu;
static acia_t acia;
static const emu_config_t config = { CLOCK_FREQ, 1843200 };
static acia_test_data_t test_data;

static void *acia_test_iface_init(void *params)
{
    return &test_data;
}

static bool acia_test_iface_avail(void *handle)
{
    return ((acia_test_data_t *)handle)->avail;
}

static uint8_t acia_test_iface_read(void *handle)
{
    test_data.read_cnt++;
    test_data.avail = false;
    return ((acia_test_data_t *)handle)->read_byte;
}

static void acia_test_iface_write(void *handle, uint8_t data)
{
    if(test_data.write_cnt == MAX_TEST_WRITE_BYTES)
    {
        return;
    }

    test_data.write_bytes[test_data.write_cnt] = data;
    test_data.write_cnt++;
}

static void acia_test_iface_cleanup(void *handle)
{
}

static const acia_trans_interface_t acia_test_iface =
{
    acia_test_iface_init,
    acia_test_iface_avail,
    acia_test_iface_read,
    acia_test_iface_write,
    acia_test_iface_cleanup
};

static uint8_t irq_test_bus_read(uint16_t addr, bus_flags_t flags, void *userdata)
{
    switch(addr)
    {
        /* Just jump to 0x8000 at reset. */
        case 0xfffc:
            return 0;
        case 0xfffd:
            return 0x80;
        /* IRQ Vector to 0xC000. */
        case 0xfffe:
            return 0;
        case 0xffff:
            return 0xC0;
        case 0xC000:
            /* Be sure that this is a opcode read. */
            if(flags & SYNC)
            {
                /* Flag that IRQ was hit. */
                *((bool *)userdata) = true;
            }

            /* Just run NOPs. */
            return 0xEA;
        case 0x8000:
            /* First opcode at reset needs to enable interrupts. */
            return 0x58;
        default:
            /* NOP for any other fetch */
            return 0xEA;
    }
}

static const bus_handlers_t irq_test_bus_handlers =
{
    NULL,
    irq_test_bus_read,
    NULL
};

void test_init_direct(void)
{
    TEST_ASSERT_NOT_NULL(emu);

    acia = acia_init(emu, &acia_test_iface, NULL, clock_get_core_clk(emu));

    TEST_ASSERT_NOT_NULL(acia);
}

void test_init_bus(void)
{
    bus_decode_params_t decoder;

    TEST_ASSERT_NOT_NULL(emu);

    decoder.type = BUSDECODE_RANGE;
    decoder.value.range.addr_start = 0x1000;
    decoder.value.range.addr_end = 0x100F;

    acia = acia_init(emu, &acia_test_iface, NULL, clock_get_core_clk(emu));

    TEST_ASSERT_NOT_NULL(acia);

    TEST_ASSERT_TRUE(acia_register(acia, &decoder, 0x1000, false));
}

void test_send_byte_16x(void)
{
    unsigned int index;

    acia = acia_init(emu, &acia_test_iface, NULL, clock_get_core_clk(emu));

    TEST_ASSERT_NOT_NULL(acia);

    /* Setup CTRL 16x baud + 8 bits + 1 Stop + Receiver internal baud */
    acia_write(acia, 0x3, 0x10);

    /* Setup CMD to enable DTR */
    acia_write(acia, 0x2, 0x01);

    /* Write a tx byte */
    acia_write(acia, 0, 0xAA);

    /* Tx should take 16 x 10 clocks. Ensure transport remains idle until last tick */
    for(index = 0; index < (16 * 10) - 1; index++)
    {
        acia_tick(acia);

        TEST_ASSERT_EQUAL_INT(0, test_data.write_cnt);
    }

    acia_tick(acia);

    /* Byte should now have been written. */
    TEST_ASSERT_EQUAL_INT(1, test_data.write_cnt);
    TEST_ASSERT_EQUAL_UINT8(0xAA, test_data.write_bytes[0]);
}

void test_send_byte_19200(void)
{
    unsigned int index;

    acia = acia_init(emu, &acia_test_iface, NULL, clock_get_core_clk(emu));

    TEST_ASSERT_NOT_NULL(acia);

    /* Setup CTRL 9600 baud + 8 bits + 1 Stop + Receiver internal baud */
    acia_write(acia, 0x3, 0x1f);

    /* Setup CMD to enable DTR */
    acia_write(acia, 0x2, 0x01);

    /* Write a tx byte */
    acia_write(acia, 0, 0xAA);

    /* Tx should take 16 x 6 * 10 clocks. Ensure transport remains idle until last tick */
    for(index = 0; index < (16 * 6 * 10) - 1; index++)
    {
        acia_tick(acia);

        TEST_ASSERT_EQUAL_INT(0, test_data.write_cnt);
    }

    acia_tick(acia);

    /* Byte should now have been written. */
    TEST_ASSERT_EQUAL_INT(1, test_data.write_cnt);
    TEST_ASSERT_EQUAL_UINT8(0xAA, test_data.write_bytes[0]);
}

void test_send_byte_16x_2stop(void)
{
    unsigned int index;

    acia = acia_init(emu, &acia_test_iface, NULL, clock_get_core_clk(emu));

    TEST_ASSERT_NOT_NULL(acia);

    /* Setup CTRL 16x baud + 8 bits + 2 Stop + Receiver internal baud */
    acia_write(acia, 0x3, 0x90);

    /* Setup CMD to enable DTR */
    acia_write(acia, 0x2, 0x01);

    /* Write a tx byte */
    acia_write(acia, 0, 0xAA);

    /* Tx should take 16 * 11 clocks. Ensure transport remains idle until last tick */
    for(index = 0; index < (16 * 11) - 1; index++)
    {
        acia_tick(acia);

        TEST_ASSERT_EQUAL_INT(0, test_data.write_cnt);
    }

    acia_tick(acia);

    /* Byte should now have been written. */
    TEST_ASSERT_EQUAL_INT(1, test_data.write_cnt);
    TEST_ASSERT_EQUAL_UINT8(0xAA, test_data.write_bytes[0]);
}

void test_send_byte_16x_5bit_1p5_stop(void)
{
    unsigned int index;

    acia = acia_init(emu, &acia_test_iface, NULL, clock_get_core_clk(emu));

    TEST_ASSERT_NOT_NULL(acia);

    /* Setup CTRL 16x baud + 5 bits + 1.5 Stop + Receiver internal baud */
    acia_write(acia, 0x3, 0xF0);

    /* Setup CMD to enable DTR */
    acia_write(acia, 0x2, 0x01);

    /* Write a tx byte */
    acia_write(acia, 0, 0xAA);

    /* Tx should take 16 * 7 clocks + 8 (1/2 stop). Ensure transport remains idle until last tick */
    for(index = 0; index < (16 * 7) + 7; index++)
    {
        acia_tick(acia);

        TEST_ASSERT_EQUAL_INT(0, test_data.write_cnt);
    }

    acia_tick(acia);

    /* Byte should now have been written. */
    TEST_ASSERT_EQUAL_INT(1, test_data.write_cnt);
    TEST_ASSERT_EQUAL_UINT8(0xAA, test_data.write_bytes[0]);
}

void test_recv_byte_16x(void)
{
    unsigned int index;
    uint8_t status;

    acia = acia_init(emu, &acia_test_iface, NULL, clock_get_core_clk(emu));

    TEST_ASSERT_NOT_NULL(acia);

    /* Setup CTRL 16x baud + 8 bits + 1 Stop + Receiver internal baud */
    acia_write(acia, 0x3, 0x10);

    /* Setup CMD to enable DTR */
    acia_write(acia, 0x2, 0x01);

    /* Setup an rx byte for available in the transport. */
    test_data.read_byte = 0x55;
    test_data.avail = true;

    /* Tx should take 16 x 10 clocks. Ensure byte is not read until last tick.
     * Note that we consume 1 extra tick in order to read avail() and start the
     * rx timer. The HW accuracy of this remains to be seen. */
    for(index = 0; index < (16 * 10); index++)
    {
        acia_tick(acia);

        TEST_ASSERT_EQUAL_INT(0, test_data.read_cnt);
    }

    acia_tick(acia);

    /* Byte should now have been written. */
    TEST_ASSERT_EQUAL_INT(1, test_data.read_cnt);

    /* Get the status register. */
    status = acia_read(acia, 0x1);

    /* Make sure RDRF flag is set. */
    TEST_ASSERT_BIT_HIGH(3, status);

    /* Read the data byte back. */
    TEST_ASSERT_EQUAL_UINT8(0x55, acia_read(acia, 0));

    /* Make sure RDRF was cleared by the read. */
    status = acia_read(acia, 0x1);
    TEST_ASSERT_BIT_LOW(3, status);
}

void test_recv_overflow(void)
{
    unsigned int index;
    uint8_t status;

    acia = acia_init(emu, &acia_test_iface, NULL, clock_get_core_clk(emu));

    TEST_ASSERT_NOT_NULL(acia);

    /* Setup CTRL 16x baud + 8 bits + 1 Stop + Receiver internal baud */
    acia_write(acia, 0x3, 0x10);

    /* Setup CMD to enable DTR */
    acia_write(acia, 0x2, 0x01);

    /* Setup an rx byte for available in the transport. */
    test_data.read_byte = 0x55;
    test_data.avail = true;

    /* Tick enough times to consume the first byte. */
    for(index = 0; index < (16 * 10) + 1; index++)
    {
        acia_tick(acia);
    }

    /* Assign a new byte to be read from the transport without clearing RDRF. */
    test_data.read_byte = 0xAA;
    test_data.avail = true;

    /* Tick enough times to consume the first byte. */
    for(index = 0; index < (16 * 10) + 1; index++)
    {
        acia_tick(acia);
    }

    /* Two bytes should now have been read. */
    TEST_ASSERT_EQUAL_INT(2, test_data.read_cnt);

    /* Get the status register. */
    status = acia_read(acia, 0x1);

    /* Make sure RDRF *and* OVERFLOW flags is set. */
    TEST_ASSERT_BITS_HIGH(0x0C, status);

    /* Read the data byte back. Is should be the first byte and the second
     * one should have been discarded. */
    TEST_ASSERT_EQUAL_UINT8(0x55, acia_read(acia, 0));

    /* Make sure RDRF nd OVER was cleared by the read. */
    status = acia_read(acia, 0x1);
    TEST_ASSERT_BITS_LOW(0x0C, status);
}

void test_recv_irq(void)
{
    unsigned int index;
    uint8_t status;
    bus_decode_params_t bus;
    bool irq_hit = false;

    acia = acia_init(emu, &acia_test_iface, NULL, clock_get_core_clk(emu));
    TEST_ASSERT_NOT_NULL(acia);

    /* Register the test memory space from 0x8000:0xFFFF */
    bus.type = BUSDECODE_RANGE;
    bus.value.range.addr_start = 0x8000;
    bus.value.range.addr_end = 0xFFFF;
    TEST_ASSERT_NOT_NULL(emu_bus_register(emu, &bus, &irq_test_bus_handlers, &irq_hit));

    /* Register the ACIA at 0x4000 and connect the interrupt. */
    bus.value.range.addr_start = 0x4000;
    bus.value.range.addr_end = 0x4003;
    acia_register(acia, &bus, 0x4000, true);

    /* Tick the emulator enough to hit the reset handler and cli. */
    for(index = 0; index < 10; index++)
    {
        emu_tick(emu);
    }

    /* Direct write to ACIA registers here just to make the test cleaner. */

    /* Setup CTRL 16x baud + 8 bits + 1 Stop + Receiver internal baud */
    acia_write(acia, 0x3, 0x10);

    /* Setup CMD to enable DTR */
    acia_write(acia, 0x2, 0x01);

    /* Setup an rx byte for available in the transport. */
    test_data.read_byte = 0x55;
    test_data.avail = true;

    /* Tick enough to consume the Rx byte + do the IRQ vector pull. */
    for(index = 0; index < (16 * 10) + 10; index++)
    {
        emu_tick(emu);
    }

    /* Bus handler should have set irq_hit if the IRQ Vector was hit. */
    TEST_ASSERT_TRUE(irq_hit);

    /* Ensure IRQ bit is set in status. */
    status = acia_read(acia, 0x01);
    TEST_ASSERT_BIT_HIGH(7, status);

    /* Ensure reading back status cleared IRQ bit. */
    status = acia_read(acia, 0x01);
    TEST_ASSERT_BIT_LOW(7, status);
}

void setUp(void)
{
    emu = emu_init(&config);
    memset(&test_data, 0, sizeof(test_data));
}

void tearDown(void)
{
    if(acia != NULL)
    {
        acia_cleanup(acia);
        acia = NULL;
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
    RUN_TEST(test_send_byte_16x);
    RUN_TEST(test_send_byte_19200);
    RUN_TEST(test_send_byte_16x_2stop);
    RUN_TEST(test_send_byte_16x_5bit_1p5_stop);
    RUN_TEST(test_recv_byte_16x);
    RUN_TEST(test_recv_overflow);
    RUN_TEST(test_recv_irq);

    return UNITY_END();
}

