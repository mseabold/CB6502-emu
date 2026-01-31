#include <unity/unity.h>
#include "bus.h"
#include "emu_priv_types.h"
#include "bus_priv.h"

typedef struct
{
    uint16_t addr;
    uint8_t val;
    bool write;
} trace_log_entry_t;

typedef struct
{
    uint8_t num_entries;
    uint8_t entries_size;
    trace_log_entry_t *entries;
} trace_log_t;

struct cbemu_s emu;
uint8_t expectedVal;
uint8_t writtenVal;

static void write_cb(uint16_t addr, uint8_t val, bus_flags_t flags, void *userdata)
{
    if(userdata != NULL)
    {
        *(uint8_t *)userdata = val;
    }
    else
    {
        writtenVal = val;
    }
}

static uint8_t read_cb(uint16_t addr, bus_flags_t flags, void *userdata)
{
    if(userdata != NULL)
    {
        return *(uint8_t *)userdata;
    }

    return expectedVal;
}

static bus_handlers_t handlers = {
    write_cb,
    read_cb,
    read_cb
};

static void trace_cb(uint16_t addr, uint8_t value, bool write, bus_flags_t flags, void *param)
{
    trace_log_t *log;

    if(param == NULL)
    {
        return;
    }

    log = (trace_log_t *)param;

    if(log->num_entries < log->entries_size)
    {
        trace_log_entry_t *entry = &log->entries[log->num_entries];

        entry->addr = addr;
        entry->val = value;
        entry->write = write;

        log->num_entries++;
    }
}

void test_register_range(void)
{
    bus_decode_params_t params;
    params.type = BUSDECODE_RANGE;
    params.value.range.addr_start = 0x1000;
    params.value.range.addr_end = 0x2000;
    TEST_ASSERT_NOT_NULL(emu_bus_register(&emu, &params, &handlers, NULL));
}

void test_invalid_range(void)
{
    bus_decode_params_t params;
    params.type = BUSDECODE_RANGE;
    params.value.range.addr_start = 0x2000;
    params.value.range.addr_end = 0x1000;

    TEST_ASSERT_NULL(emu_bus_register(&emu, &params, &handlers, NULL));
}

void test_register_mask(void)
{
    bus_decode_params_t params;
    params.type = BUSDECODE_MASK;
    params.value.mask.addr_mask = 0xAAAA;
    params.value.mask.addr_value = 0x0A0A;

    TEST_ASSERT_NOT_NULL(emu_bus_register(&emu, &params, &handlers, NULL));
}

void test_invalid_mask(void)
{
    bus_decode_params_t params;
    params.type = BUSDECODE_MASK;
    params.value.mask.addr_mask = 0;

    TEST_ASSERT_NULL(emu_bus_register(&emu, &params, &handlers, NULL));
}


void test_unregister(void)
{
    bus_decode_params_t params;
    bus_cb_handle_t handle;
    params.type = BUSDECODE_RANGE;
    params.value.range.addr_start = 0x1000;
    params.value.range.addr_end = 0x2000;

    expectedVal = 0xAA;

    handle = emu_bus_register(&emu, &params, &handlers, NULL);
    TEST_ASSERT_NOT_NULL(handle);
    TEST_ASSERT_EQUAL_UINT8(expectedVal, bus_read(&emu, 0x1000));

    emu_bus_unregister(&emu, handle);
    TEST_ASSERT_EQUAL_UINT8(0xFF, bus_read(&emu, 0x1000));
}

void test_bus_range(void)
{
    bus_decode_params_t params;
    bus_cb_handle_t handle;

    params.type = BUSDECODE_RANGE;
    params.value.range.addr_start = 0x2000;
    params.value.range.addr_end = 0x2FFF;

    expectedVal = 0xAA;

    handle = emu_bus_register(&emu, &params, &handlers, NULL);
    TEST_ASSERT_NOT_NULL(handle);

    /* Check bus returns expected value within range. */
    TEST_ASSERT_EQUAL_UINT8(expectedVal, bus_read(&emu, 0x2800));

    /* Check beginning boundary. */
    TEST_ASSERT_EQUAL_UINT8(0xFF, bus_read(&emu, 0x1FFF));
    TEST_ASSERT_EQUAL_UINT8(expectedVal, bus_read(&emu, 0x2000));

    /* Check end boundary. */
    TEST_ASSERT_EQUAL_UINT8(expectedVal, bus_read(&emu, 0x2FFF));
    TEST_ASSERT_EQUAL_UINT8(0xFF, bus_read(&emu, 0x3000));

    /* Check bus returns default value (FF) out of range. */
    TEST_ASSERT_EQUAL_UINT8(0xFF, bus_read(&emu, 0x8000));
}

void test_bus_mask(void)
{
    bus_decode_params_t params;
    bus_cb_handle_t handle;

    params.type = BUSDECODE_MASK;
    params.value.mask.addr_mask = 0x8000;
    params.value.mask.addr_value = 0x8000;

    handle = emu_bus_register(&emu, &params, &handlers, NULL);

    TEST_ASSERT_NOT_NULL(handle);

    expectedVal = 0xAA;

    TEST_ASSERT_EQUAL_UINT8(expectedVal, bus_read(&emu, 0x8000));
    TEST_ASSERT_EQUAL_UINT8(0xFF, bus_read(&emu, 0x0000));
}

void test_register_unregister_mult(void)
{
    bus_decode_params_t params;
    bus_decode_cb_t handle1, handle2;
    uint8_t exp1, exp2;

    exp1 = 0x55;
    exp2 = 0xAA;

    params.type = BUSDECODE_RANGE;
    params.value.range.addr_start = 0x0000;
    params.value.range.addr_end = 0x8000;

    handle1 = emu_bus_register(&emu, &params, &handlers, &exp1);
    TEST_ASSERT_NOT_NULL(handle1);

    TEST_ASSERT_EQUAL_UINT8(0x55, bus_read(&emu, 0x2000));
    TEST_ASSERT_EQUAL_UINT8(0xFF, bus_read(&emu, 0x9000));

    params.type = BUSDECODE_RANGE;
    params.value.range.addr_start = 0x8000;
    params.value.range.addr_end = 0xFFFF;

    handle2 = emu_bus_register(&emu, &params, &handlers, &exp2);
    TEST_ASSERT_NOT_NULL(handle2);

    TEST_ASSERT_EQUAL_UINT8(0x55, bus_read(&emu, 0x2000));
    TEST_ASSERT_EQUAL_UINT8(0xAA, bus_read(&emu, 0x9000));

    emu_bus_unregister(&emu, handle1);

    TEST_ASSERT_EQUAL_UINT8(0xFF, bus_read(&emu, 0x2000));
    TEST_ASSERT_EQUAL_UINT8(0xAA, bus_read(&emu, 0x9000));

    emu_bus_unregister(&emu, handle2);

    TEST_ASSERT_EQUAL_UINT8(0xFF, bus_read(&emu, 0x2000));
    TEST_ASSERT_EQUAL_UINT8(0xFF, bus_read(&emu, 0x9000));
}

void test_tracer(void)
{
    trace_log_entry_t entries[3];
    trace_log_entry_t expEntries[2];
    trace_log_t log;
    bus_cb_handle_t handle;
    bus_decode_params_t params;
    uint8_t i;

    log.entries = entries;
    log.num_entries = 0;
    log.entries_size = 3;

    handle = emu_bus_add_tracer(&emu, trace_cb, &log);

    TEST_ASSERT_NOT_NULL(handle);

    params.type = BUSDECODE_RANGE;
    params.value.range.addr_start = 0x0000;
    params.value.range.addr_end = 0xFFFF;

    TEST_ASSERT_NOT_NULL(emu_bus_register(&emu, &params, &handlers, NULL));

    expectedVal = 0xAA;
    expEntries[0].addr = 0x1000;
    expEntries[0].val = expectedVal;
    expEntries[0].write = false;
    bus_read(&emu, 0x1000);

    expEntries[1].addr = 0x2000;
    expEntries[1].val = 0x55;
    expEntries[1].write = true;
    bus_write(&emu, 0x2000, 0x55);

    emu_bus_remove_tracer(&emu, handle);

    bus_write(&emu, 0xA5A5, 0xFF);

    TEST_ASSERT_EQUAL_UINT8(2, log.num_entries);

    for(i = 0; i < log.num_entries; i++)
    {
        TEST_ASSERT_EQUAL_UINT16(expEntries[i].addr, log.entries[i].addr);
        TEST_ASSERT_EQUAL_UINT8(expEntries[i].val, log.entries[i].val);
        TEST_ASSERT_TRUE(expEntries[i].write == log.entries[i].write);
    }
}

void setUp(void)
{
    bus_init(&emu);
}

void tearDown(void)
{
    bus_cleanup(&emu);
}

int main(int argc, char *argv[])
{
    UNITY_BEGIN();
    RUN_TEST(test_register_range);
    RUN_TEST(test_invalid_range);
    RUN_TEST(test_unregister);
    RUN_TEST(test_register_mask);
    RUN_TEST(test_invalid_mask);
    RUN_TEST(test_bus_range);
    RUN_TEST(test_bus_mask);
    RUN_TEST(test_register_unregister_mult);
    RUN_TEST(test_tracer);
    return UNITY_END();
}
