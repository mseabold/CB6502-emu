#include <unity/unity.h>
#include "emulator.h"
#include "via.h"

static cbemu_t emu;
static via_t via;
static const emu_config_t config = { CLOCK_FREQ, 1000000 };

void test_init_direct(void)
{
    via = via_init(NULL);

    TEST_ASSERT_NOT_NULL(via);
}

void test_init_bus(void)
{
    bus_decode_params_t decoder;
    io_bus_params_t iop;

    emu = emu_init(&config);

    TEST_ASSERT_NOT_NULL(emu);

    decoder.type = BUSDECODE_RANGE;
    decoder.value.range.addr_start = 0x1000;
    decoder.value.range.addr_end = 0x100F;
    iop.emulator = emu;
    iop.decoder = &decoder;
    iop.base = 0x1000;

    via = via_init(&iop);

    TEST_ASSERT_NOT_NULL(via);
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
