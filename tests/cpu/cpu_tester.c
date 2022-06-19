#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <unity/unity.h>
#include "cpu.h"
#include "sys.h"

typedef struct mem_expect_s
{
    bool rwb;
    uint16_t addr;
    uint8_t val;
} mem_expect_t;

const mem_expect_t test_cpu_reset_mem_expect[] =
{
    {true, 0xfffc, 0x34},
    {true, 0xfffd, 0x12},
    {true, 0x1234, 0xea}
};

const mem_expect_t test_cpu_irq_exepect[] =
{
    {true, 0xfffc, 0x34},
    {true, 0xfffd, 0x12},
    {true, 0x1234, 0x58},
    {false, 0x01fd, 0x12},
    {false, 0x01fc, 0x35},
    {false, 0x01fb, 0x20},
    {true, 0xfffe, 0x78},
    {true, 0xffff, 0x56},
};

static const mem_expect_t *test_mem_calls;
static unsigned int num_test_mem_calls;
static unsigned int test_mem_idx;

void test_mem_write(uint16_t addr, uint8_t val)
{
    printf("Write 0x%04x 0x%02x\n", addr, val);
    TEST_ASSERT(test_mem_idx < num_test_mem_calls);
    TEST_ASSERT_FALSE(test_mem_calls[test_mem_idx].rwb);
    TEST_ASSERT(test_mem_calls[test_mem_idx].addr == addr);
    TEST_ASSERT(test_mem_calls[test_mem_idx].val == val);
    ++test_mem_idx;
}

uint8_t test_mem_read(uint16_t addr)
{
    uint8_t val;

    printf("Read 0x%04x\n", addr);
    TEST_ASSERT(test_mem_idx < num_test_mem_calls);
    TEST_ASSERT(test_mem_calls[test_mem_idx].rwb);
    TEST_ASSERT(test_mem_calls[test_mem_idx].addr == addr);

    val = test_mem_calls[test_mem_idx].val;

    ++test_mem_idx;
    return val;
}



void test_cpu_reset_mem(void)
{
    sys_cxt_t cxt = sys_init(test_mem_read, test_mem_write);

    TEST_ASSERT_NOT_NULL(cxt);

    test_mem_calls = test_cpu_reset_mem_expect;
    num_test_mem_calls = 3;
    test_mem_idx = 0;

    cpu_init(cxt, true);
    cpu_step();

    TEST_ASSERT_EQUAL_UINT(test_mem_idx, 3);
}

void test_cpu_irq(void)
{
    sys_cxt_t cxt = sys_init(test_mem_read, test_mem_write);

    TEST_ASSERT_NOT_NULL(cxt);

    test_mem_calls = test_cpu_irq_exepect;
    num_test_mem_calls = 8;
    test_mem_idx = 0;

    cpu_init(cxt, true);
    cpu_step();
    sys_vote_interrupt(cxt, false, true);
    TEST_ASSERT(sys_check_interrupt(cxt, false));
    printf("step\n");
    cpu_step();
}

void setUp(void)
{
}

void tearDown(void)
{
}

int main(int argc, char *argv[])
{
    UNITY_BEGIN();
    RUN_TEST(test_cpu_reset_mem);
    RUN_TEST(test_cpu_irq);
    return UNITY_END();
}
