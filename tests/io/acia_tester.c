#include <unity/unity.h>
#include "emulator.h"
#include "acia.h"
#include "acia_console.h"


static cbemu_t emu;
static acia_t acia;
static const emu_config_t config = { CLOCK_FREQ, 1000000 };

void test_init_direct(void)
{
    emu = emu_init(&config);

    TEST_ASSERT_NOT_NULL(emu);

    acia = acia_init(acia_console_get_iface(), NULL, clock_get_core_clk(emu));

    TEST_ASSERT_NOT_NULL(acia);
}

void test_init_bus(void)
{
    bus_decode_params_t decoder;

    emu = emu_init(&config);

    TEST_ASSERT_NOT_NULL(emu);

    decoder.type = BUSDECODE_RANGE;
    decoder.value.range.addr_start = 0x1000;
    decoder.value.range.addr_end = 0x100F;

    acia = acia_init(acia_console_get_iface(), NULL, clock_get_core_clk(emu));

    TEST_ASSERT_NOT_NULL(acia);

    TEST_ASSERT_TRUE(acia_register(acia, emu, &decoder, 0x1000));
}

void setUp(void)
{
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

    return UNITY_END();
}

