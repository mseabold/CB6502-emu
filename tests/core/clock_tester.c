#include <unity/unity.h>

#include "clock.h"
#include "emu_priv_types.h"
#include "clock_priv.h"
#include "log.h"
#include "console_log.h"

#define MAIN_CLK_FREQ 1000000

struct cbemu_s emu;

static void bool_tick_cb(clk_t clk, void *userdata)
{
    *(bool *)userdata = true;
}

static void counter_tick_cb(clk_t clk, void *userdata)
{
    *(uint32_t *)userdata += 1;
}

static void main_tick_handler(clk_t clk, void *userdata)
{
}

void test_clock_add(void)
{
    clk_t clk;
    clock_config_t config;
    config.timing_type = CLOCK_FREQ;
    config.timing.freq = 5000000;

    clk = clock_add(&emu, &config);

    TEST_ASSERT_NOT_NULL(clk);
    TEST_ASSERT_EQUAL_UINT32(200, clk->period);
}

void test_clock_add_freq_round(void)
{
    clk_t clk;
    clock_config_t config;
    config.timing_type = CLOCK_FREQ;

    /* Pick a frequency that should cause a round-up when converting to ns. */
    config.timing.freq = 1843200;

    clk = clock_add(&emu, &config);

    TEST_ASSERT_NOT_NULL(clk);

    /* Verify resultant period is rounded up. */
    TEST_ASSERT_EQUAL_UINT32(543, clk->period);
}

void test_add_clock_period(void)
{
    clk_t clk;
    clock_config_t config;
    config.timing_type = CLOCK_PERIOD;

    /* Pick a period that should cause a round-up when converting to hz. */
    config.timing.freq = 204;

    clk = clock_add(&emu, &config);

    TEST_ASSERT_NOT_NULL(clk);

    /* Verify resultant period is rounded up. */
    TEST_ASSERT_EQUAL_UINT32(4901961, clk->freq);
}

void test_register_tick_main(void)
{
    clock_cb_handle_t handle;

    handle = clock_register_tick(emu.clk.mainClk, bool_tick_cb, NULL);

    TEST_ASSERT_NOT_NULL(handle);
}

void test_main_clock_tick(void)
{
    bool ticked = false;
    clock_cb_handle_t handle;

    handle = clock_register_tick(emu.clk.mainClk, bool_tick_cb, &ticked);

    clock_main_tick(&emu);

    TEST_ASSERT_TRUE(ticked);
}

void test_two_clocks_half_freq(void)
{
    clk_t clk;
    uint32_t mainCnt = 0;
    uint32_t secCnt = 0;
    clock_config_t config;

    config.timing_type = CLOCK_FREQ;
    config.timing.freq = MAIN_CLK_FREQ/2;

    clk = clock_add(&emu, &config);

    TEST_ASSERT_NOT_NULL(clk);

    TEST_ASSERT_NOT_NULL(clock_register_tick(emu.clk.mainClk, counter_tick_cb, &mainCnt));
    TEST_ASSERT_NOT_NULL(clock_register_tick(clk, counter_tick_cb, &secCnt));

    clock_main_tick(&emu);

    TEST_ASSERT_EQUAL_UINT32(1, mainCnt);
    TEST_ASSERT_EQUAL_UINT32(0, secCnt);

    clock_main_tick(&emu);

    TEST_ASSERT_EQUAL_UINT32(2, mainCnt);
    TEST_ASSERT_EQUAL_UINT32(1, secCnt);
}

void test_three_clocks_half_double(void)
{
    clk_t clk, clk2;
    uint32_t mainCnt = 0;
    uint32_t secCnt = 0;
    uint32_t thrdCnt = 0;
    clock_config_t config;

    config.timing_type = CLOCK_FREQ;
    config.timing.freq = MAIN_CLK_FREQ/2;
    clk = clock_add(&emu, &config);

    config.timing.freq = MAIN_CLK_FREQ*2;
    clk2 = clock_add(&emu, &config);

    TEST_ASSERT_NOT_NULL(clk);
    TEST_ASSERT_NOT_NULL(clk2);

    TEST_ASSERT_NOT_NULL(clock_register_tick(emu.clk.mainClk, counter_tick_cb, &mainCnt));
    TEST_ASSERT_NOT_NULL(clock_register_tick(clk, counter_tick_cb, &secCnt));
    TEST_ASSERT_NOT_NULL(clock_register_tick(clk2, counter_tick_cb, &thrdCnt));

    clock_main_tick(&emu);

    TEST_ASSERT_EQUAL_UINT32(1, mainCnt);
    TEST_ASSERT_EQUAL_UINT32(0, secCnt);
    TEST_ASSERT_EQUAL_UINT32(2, thrdCnt);

    clock_main_tick(&emu);

    TEST_ASSERT_EQUAL_UINT32(2, mainCnt);
    TEST_ASSERT_EQUAL_UINT32(1, secCnt);
    TEST_ASSERT_EQUAL_UINT32(4, thrdCnt);
}

void setUp(void)
{
    clock_config_t config;
    config.timing_type = CLOCK_FREQ;
    config.timing.freq = MAIN_CLK_FREQ;

    /* Bypass the internal clock registration of the emulator to unit test. */
    clock_init(&emu, &config, main_tick_handler);
}

void tearDown(void)
{
    clock_cleanup(&emu);
}

int main(int argc, char *argv[])
{
    log_set_handler(console_log_print);
    log_set_level(lDEBUG);
    UNITY_BEGIN();
    RUN_TEST(test_clock_add);
    RUN_TEST(test_clock_add_freq_round);
    RUN_TEST(test_add_clock_period);
    RUN_TEST(test_register_tick_main);
    RUN_TEST(test_main_clock_tick);
    RUN_TEST(test_two_clocks_half_freq);
    RUN_TEST(test_three_clocks_half_double);
    return UNITY_END();
}
