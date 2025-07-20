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

void test_clock_add(void)
{
    clk_t clk;

    clk = clock_add(&emu, 500000);

    TEST_ASSERT_NOT_NULL(clk);
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

    clock_main_tick(&emu.clk);

    TEST_ASSERT_TRUE(ticked);
}

void test_two_clocks_half_freq(void)
{
    clk_t clk;
    uint32_t mainCnt = 0;
    uint32_t secCnt = 0;

    clk = clock_add(&emu, MAIN_CLK_FREQ/2);

    TEST_ASSERT_NOT_NULL(clk);

    TEST_ASSERT_NOT_NULL(clock_register_tick(emu.clk.mainClk, counter_tick_cb, &mainCnt));
    TEST_ASSERT_NOT_NULL(clock_register_tick(clk, counter_tick_cb, &secCnt));

    clock_main_tick(&emu.clk);

    TEST_ASSERT_EQUAL_UINT32(1, mainCnt);
    TEST_ASSERT_EQUAL_UINT32(2, secCnt);
}

void test_three_clocks_half_double(void)
{
    clk_t clk, clk2;
    uint32_t mainCnt = 0;
    uint32_t secCnt = 0;
    uint32_t thrdCnt = 0;

    clk = clock_add(&emu, MAIN_CLK_FREQ/2);
    clk2 = clock_add(&emu, MAIN_CLK_FREQ*2);

    TEST_ASSERT_NOT_NULL(clk);
    TEST_ASSERT_NOT_NULL(clk2);

    TEST_ASSERT_NOT_NULL(clock_register_tick(emu.clk.mainClk, counter_tick_cb, &mainCnt));
    TEST_ASSERT_NOT_NULL(clock_register_tick(clk, counter_tick_cb, &secCnt));
    TEST_ASSERT_NOT_NULL(clock_register_tick(clk2, counter_tick_cb, &thrdCnt));

    clock_main_tick(&emu.clk);

    TEST_ASSERT_EQUAL_UINT32(1, mainCnt);
    TEST_ASSERT_EQUAL_UINT32(2, secCnt);
    TEST_ASSERT_EQUAL_UINT32(0, thrdCnt);

    clock_main_tick(&emu.clk);

    TEST_ASSERT_EQUAL_UINT32(2, mainCnt);
    TEST_ASSERT_EQUAL_UINT32(4, secCnt);
    TEST_ASSERT_EQUAL_UINT32(1, thrdCnt);
}

void setUp(void)
{
    clock_init(&emu.clk, MAIN_CLK_FREQ);
}

void tearDown(void)
{
    clock_cleanup(&emu.clk);
}

int main(int argc, char *argv[])
{
    log_set_handler(console_log_print);
    log_set_level(lDEBUG);
    UNITY_BEGIN();
    RUN_TEST(test_clock_add);
    RUN_TEST(test_register_tick_main);
    RUN_TEST(test_main_clock_tick);
    RUN_TEST(test_two_clocks_half_freq);
    RUN_TEST(test_three_clocks_half_double);
    return UNITY_END();
}
