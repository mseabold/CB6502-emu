#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <unity/unity.h>

#include "sys.h"
#include "at28c256.h"

static sys_cxt_t sys;
static at28c256_t iut;

uint8_t read_hlr(uint16_t addr)
{
    return 0xff;
}

void write_hlr(uint16_t addr, uint8_t val)
{
}

static const mem_space_t mem_space =
{
    write_hlr,
    read_hlr,
    read_hlr
};

void setUp(void)
{
    sys = sys_init(&mem_space);
    iut = at28c256_init(sys, 0);
}

void tearDown(void)
{
    at28c256_destroy(iut);
    sys_destroy(sys);
}

static const uint8_t TEST_DATA[] =
{
    0x00, 0x01, 0x02, 0x03
};

void test_image_load(void)
{
    TEST_ASSERT(at28c256_load_image(iut, sizeof(TEST_DATA), (uint8_t *)TEST_DATA, 0));
    TEST_ASSERT_EQUAL_UINT8(TEST_DATA[0], at28c256_read(iut, 0));
    TEST_ASSERT_EQUAL_UINT8(TEST_DATA[1], at28c256_read(iut, 1));
    TEST_ASSERT_EQUAL_UINT8(TEST_DATA[2], at28c256_read(iut, 2));
    TEST_ASSERT_EQUAL_UINT8(TEST_DATA[3], at28c256_read(iut, 3));
}

void test_image_load_offset(void)
{
    TEST_ASSERT(at28c256_load_image(iut, sizeof(TEST_DATA), (uint8_t *)TEST_DATA, 1));
    TEST_ASSERT_EQUAL_UINT8(0xFF, at28c256_read(iut, 0));
    TEST_ASSERT_EQUAL_UINT8(TEST_DATA[0], at28c256_read(iut, 1));
    TEST_ASSERT_EQUAL_UINT8(TEST_DATA[1], at28c256_read(iut, 2));
    TEST_ASSERT_EQUAL_UINT8(TEST_DATA[2], at28c256_read(iut, 3));
    TEST_ASSERT_EQUAL_UINT8(TEST_DATA[3], at28c256_read(iut, 4));
}

void test_image_load_offset_too_large(void)
{
    TEST_ASSERT_FALSE(at28c256_load_image(iut, sizeof(TEST_DATA), (uint8_t *)TEST_DATA, 0x7ffd));
}

void test_single_byte_write(void)
{
    uint8_t write_byte, read_byte, last_read;

    write_byte = 0xAA;

    at28c256_write(iut, 0x0000, write_byte);

    read_byte = at28c256_read(iut, 0x0000);

    TEST_ASSERT_BIT_LOW(7, read_byte);

    last_read = read_byte;
    read_byte = at28c256_read(iut, 0x0000);
    TEST_ASSERT((last_read ^ read_byte) == 0x40);

    last_read = read_byte;

    at28c256_tick(iut, 160);

    at28c256_tick(iut, 15000);

    TEST_ASSERT(at28c256_read(iut, 0x0000) == write_byte);
}

void test_partial_page_write(void)
{
    uint8_t large_test_data[64];
    uint8_t i;

    for(i=0;i<64;++i)
    {
        large_test_data[i] = 0xee;
    }

    TEST_ASSERT(at28c256_load_image(iut, sizeof(large_test_data), large_test_data, 0x1000));

    at28c256_write(iut, 0x1004, 0x04);
    large_test_data[0x04] = 0x04;
    at28c256_tick(iut, 8);

    at28c256_write(iut, 0x1010, 0x10);
    large_test_data[0x10] = 0x10;
    at28c256_tick(iut, 8);

    at28c256_write(iut, 0x1001, 0x01);
    large_test_data[0x01] = 0x01;
    at28c256_tick(iut, 8);

    at28c256_write(iut, 0x103f, 0x3f);
    large_test_data[0x3f] = 0x3f;

    at28c256_tick(iut, 15000);

    for(i=0;i<64;++i)
    {
        TEST_ASSERT_EQUAL_UINT8(large_test_data[i], at28c256_read(iut, 0x1000+i));
    }
}

int main(int argc, char *argv[])
{
    UNITY_BEGIN();
    RUN_TEST(test_image_load);
    RUN_TEST(test_image_load_offset);
    RUN_TEST(test_image_load_offset_too_large);
    RUN_TEST(test_single_byte_write);
    RUN_TEST(test_partial_page_write);
    return UNITY_END();
}
