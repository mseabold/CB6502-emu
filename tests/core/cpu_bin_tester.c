#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <unity/unity.h>
#include <unity_internals.h>
#include "emulator.h"
#include "cpu_bin_tests.h"
#include "cpu_priv.h"

#define BUSLOG_MAX 10

typedef struct
{
    uint16_t addr;
    uint8_t value;
    bool write;
} bus_log_entry_t;

typedef struct
{
    uint8_t log_cnt;
    bus_log_entry_t entries[BUSLOG_MAX];
} bus_log_t;

static cbemu_t emu;
static const cpu_bin_test_info_t *cur_info;
static uint8_t memory[0x10000];
static bus_log_t buslog;
static bool in_opcode;
static FILE *testfile;

static void write_mem(uint16_t addr, uint8_t val, bus_flags_t flags, void *userdata)
{
    if(in_opcode && buslog.log_cnt < BUSLOG_MAX)
    {
        bus_log_entry_t *entry = &buslog.entries[buslog.log_cnt++];
        entry->write = true;
        entry->addr = addr;
        entry->value = val;
    }

    memory[addr] = val;
}

static uint8_t read_mem(uint16_t addr, bus_flags_t flags, void *userdata)
{
    if(in_opcode && buslog.log_cnt < BUSLOG_MAX)
    {
        bus_log_entry_t *entry = &buslog.entries[buslog.log_cnt++];
        entry->write = false;
        entry->addr = addr;
        entry->value = memory[addr];
    }
    return memory[addr];
}

static uint8_t peek_mem(uint16_t addr, bus_flags_t flags, void *userdata)
{
    return memory[addr];
}

static const bus_handlers_t handlers = {
    write_mem,
    read_mem,
    peek_mem
};

static const uint8_t vectors[] = {
    0x00, 0x00,
    0x00, 0x20,
    0x00, 0x00
};

static void run_bin_test(void)
{
    bus_decode_params_t params;

    TEST_ASSERT_NOT_NULL(emu);

    params.type = BUSDECODE_RANGE;
    params.value.range.addr_start = 0x0000;
    params.value.range.addr_end = 0xffff;

    TEST_ASSERT_NOT_NULL(emu_bus_register(emu, &params, &handlers, &buslog));

    testfile = fopen(cur_info->file, "rb");

    TEST_ASSERT_NOT_NULL(testfile);

    fread(&memory[0x2000], 1, 0x1000, testfile);

    do
    {
        if(cpu_is_sync(emu) && cpu_get_pc(emu) == *((uint16_t *)memory))
        {
            in_opcode = true;
        }
        else
        {
            emu_tick(emu);
        }
    } while(!in_opcode);

    do
    {
        emu_tick(emu);
    } while(!cpu_is_sync(emu));

    in_opcode = false;

    TEST_ASSERT_EQUAL_UINT8(cur_info->cycles, buslog.log_cnt);
}

void setUp(void)
{
    emu_config_t config;

    /* Init memory */
    memset(memory, 0, sizeof(memory));
    memcpy(&memory[0xfffa], vectors, sizeof(vectors));

    /* Init test op address to invalid */
    memory[0] = 0xff;
    memory[1] = 0xff;

    config.mainclk_config.timing_type = CLOCK_FREQ;
    config.mainclk_config.timing.freq = 1000000;
    emu = emu_init(&config);

    in_opcode = false;
    memset(&buslog, 0, sizeof(buslog));
}

void tearDown(void)
{
    if(emu != NULL)
    {
        emu_cleanup(emu);
        emu = NULL;
    }

    if(testfile != NULL)
    {
        fclose(testfile);
        testfile = NULL;
    }
}

int main(int argc, char *argv[])
{
    uint32_t index;

    UNITY_BEGIN();

    for(index = 0; index < cpu_num_bin_tests; index++)
    {
        cur_info = &cpu_bin_tests[index];

        UnityDefaultTestRun(run_bin_test, cur_info->name, __LINE__);
    }

    return UNITY_END();
}
