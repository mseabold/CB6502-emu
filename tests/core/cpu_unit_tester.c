#include <unity/unity.h>

#include "bus.h"
#include "emulator.h"
#include "cpu_priv.h"

typedef struct
{
    uint16_t addr;
    uint8_t value;
    bool write;
} bus_log_entry_t;

typedef struct
{
    uint8_t log_size;
    uint8_t log_cnt;
    bus_log_entry_t *entries;
} bus_log_t;

static cbemu_t emu;
static const emu_config_t config = { 1000000 };

static uint8_t rst_vec_read_cb(uint16_t addr, void *userdata)
{
    if(addr == 0xfffc)
        return 0xAA;
    if(addr == 0xfffd)
        return 0x55;
    return 0xFF;
}

static const bus_handlers_t rst_vec_handlers = {
    NULL,
    rst_vec_read_cb,
    NULL
};

static void log_tracer_cb(uint16_t addr, uint8_t value, bool write, void *userdata)
{
    bus_log_t *log = (bus_log_t *)userdata;
    bus_log_entry_t *entry;

    if((log == NULL) || (log->log_cnt == log->log_size))
    {
        return;
    }

    entry = &log->entries[log->log_cnt++];
    entry->addr = addr;
    entry->value = value;
    entry->write = write;
}

void test_init_rst(void)
{
    bus_decode_params_t params;
    bus_log_entry_t log_entries[10];
    bus_log_t log;

    log.log_size = 10;
    log.log_cnt = 0;
    log.entries = log_entries;

    emu = emu_init(&config);
    TEST_ASSERT_NOT_NULL(emu);

    /* Register the whole memory space. */
    params.type = BUSDECODE_RANGE;
    params.value.range.addr_start = 0;
    params.value.range.addr_end = 0xffff;
    TEST_ASSERT_NOT_NULL(emu_bus_register(emu, &params, &rst_vec_handlers, NULL));

    /* Register the tracer */
    TEST_ASSERT_NOT_NULL(emu_bus_add_tracer(emu, log_tracer_cb, &log));

    /* Tick 8 times to consume the reset sequence and the first opcode post-reset */
    cpu_tick(emu);
    cpu_tick(emu);
    cpu_tick(emu);
    cpu_tick(emu);
    cpu_tick(emu);
    cpu_tick(emu);
    cpu_tick(emu);
    cpu_tick(emu);

    /* Don't really care about the fetch/write values for most of the sequence right now,
     * but check a) the vector fetch is correct and b) the jump to the vector is correct. */

    TEST_ASSERT_EQUAL_UINT8(8, log.log_cnt);
    TEST_ASSERT_EQUAL_UINT16(0xfffc, log.entries[5].addr);
    TEST_ASSERT_EQUAL_UINT8(0xAA, log.entries[5].value);
    TEST_ASSERT_FALSE(log.entries[5].write);
    TEST_ASSERT_EQUAL_UINT16(0xfffd, log.entries[6].addr);
    TEST_ASSERT_EQUAL_UINT8(0x55, log.entries[6].value);
    TEST_ASSERT_FALSE(log.entries[6].write);
    TEST_ASSERT_FALSE(log.entries[7].write);
    TEST_ASSERT_EQUAL_UINT16(0x55AA, log.entries[7].addr);
}

void setUp(void)
{
}

void tearDown(void)
{
    if(emu != NULL)
    {
        emu_cleanup(emu);
        emu = NULL;
    }
}

int main(int argc, char *argv[])
{
    UNITY_BEGIN();

    RUN_TEST(test_init_rst);

    return UNITY_END();
}
