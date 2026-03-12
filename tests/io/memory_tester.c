#include <unity/unity.h>
#include "emulator.h"
#include "memory.h"

#include "bus_priv.h"

static cbemu_t emu;
static memory_t memory;
static const emu_config_t config = { CLOCK_FREQ, 1000000 };

void test_basic_init(void)
{
    memory = memory_init(0x1000, 0);

    TEST_ASSERT_NOT_NULL(memory);
}

void test_ram_direct_rw(void)
{
    memory = memory_init(0x1000, 0);

    TEST_ASSERT_NOT_NULL(memory);

    /* Test write to beginning */
    memory_write(memory, 0, 0xAA);
    TEST_ASSERT_EQUAL_UINT8(0xAA, memory_read(memory, 0));

    /* Test write to end */
    memory_write(memory, 0x0fff, 0x55);
    TEST_ASSERT_EQUAL_UINT8(0x55, memory_read(memory, 0x0fff));

    /* Test write to middle */
    memory_write(memory, 0x0555, 0xA5);
    TEST_ASSERT_EQUAL_UINT8(0xA5, memory_read(memory, 0x0555));

    /* Try writing off the end to make sure nothing happens. */
    memory_write(memory, 0x1000, 0x5A);
}

void test_rom_direct_rw(void)
{
    uint8_t curVal;
    memory = memory_init(0x100, MEMFLAG_ROM);

    TEST_ASSERT_NOT_NULL(memory);

    /* This should always be 0, but the test doesn't need to depend on that. */
    curVal = memory_read(memory, 0);

    /* Try to write a different value. */
    memory_write(memory, 0, curVal ^ 0xff);

    TEST_ASSERT_EQUAL_UINT8(curVal, memory_read(memory, 0));
}

void test_ram_bus_rw(void)
{
    bus_decode_params_t decoder;

    emu = emu_init(&config);
    TEST_ASSERT_NOT_NULL(emu);

    decoder.type = BUSDECODE_RANGE;
    decoder.value.range.addr_start = 0x1000;
    decoder.value.range.addr_end = 0x1FFF;

    memory = memory_init(0x1000, 0);

    TEST_ASSERT_NOT_NULL(memory);

    TEST_ASSERT_TRUE(memory_register(memory, emu, &decoder, 0x1000));

    /* Test write to beginning */
    bus_write(emu, 0x1000, 0xAA);
    TEST_ASSERT_EQUAL_UINT8(0xAA, memory_read(memory, 0));

    /* Test write to end */
    bus_write(emu, 0x1fff, 0x55);
    TEST_ASSERT_EQUAL_UINT8(0x55, memory_read(memory, 0x0fff));

    /* Test write to middle */
    bus_write(emu, 0x1555, 0xA5);
    TEST_ASSERT_EQUAL_UINT8(0xA5, memory_read(memory, 0x0555));

    /* Try writing off the end to make sure nothing happens. */
    bus_write(emu, 0x2000, 0x5A);
}

void test_rom_load_full(void)
{
    uint8_t loadBuf[256];
    unsigned int index;

    for(index = 0; index < sizeof(loadBuf); index++)
    {
        loadBuf[index] = (uint8_t)index;
    }

    memory = memory_init(sizeof(loadBuf), MEMFLAG_ROM);

    TEST_ASSERT_NOT_NULL(memory);

    memory_load_data(memory, sizeof(loadBuf), loadBuf, 0, false, 0);

    for(index = 0; index < sizeof(loadBuf); index++)
    {
        TEST_ASSERT_EQUAL_UINT8(loadBuf[index], memory_read(memory, index));
    }
}

void test_rom_load_fill(void)
{
    uint8_t loadBuf[128];
    unsigned int index;

    for(index = 0; index < sizeof(loadBuf); index++)
    {
        loadBuf[index] = (uint8_t)index;
    }

    memory = memory_init(sizeof(loadBuf), MEMFLAG_ROM);

    TEST_ASSERT_NOT_NULL(memory);

    memory_load_data(memory, sizeof(loadBuf), loadBuf, 64, true, 0xAA);

    for(index = 0; index < sizeof(loadBuf); index++)
    {
        if((index < 64) || (index >= 192))
        {
            TEST_ASSERT_EQUAL_UINT8(0xAA, memory_read(memory, index));
        }
        else
        {
            TEST_ASSERT_EQUAL_UINT8(loadBuf[index-64], memory_read(memory, index));
        }
    }
}


void setUp(void)
{
}

void tearDown(void)
{
    if(memory != NULL)
    {
        memory_cleanup(memory);
        memory = NULL;
    }

    if(emu != NULL)
    {
        emu_cleanup(emu);
        emu = NULL;
    }
}

int main(int argc, char *argv[])
{
    UNITY_BEGIN();

    RUN_TEST(test_basic_init);
    RUN_TEST(test_ram_direct_rw);
    RUN_TEST(test_rom_direct_rw);
    RUN_TEST(test_ram_bus_rw);
    RUN_TEST(test_rom_load_full);
    RUN_TEST(test_rom_load_fill);

    return UNITY_END();
}

