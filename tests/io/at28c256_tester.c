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

void test_sdp_block(void)
{
    uint8_t read1;
    uint8_t read2;

    at28c256_set_sdp_enable(iut, true);

    at28c256_write(iut, 0x0000, 0xAA);
    at28c256_tick(iut, 8);

    /* Poll and stroke bits should still work. */
    read1 = at28c256_read(iut, 0x0000);

    TEST_ASSERT_BIT_LOW(7, read1);

    at28c256_tick(iut, 100);

    read2 = at28c256_read(iut, 0x0000);

    TEST_ASSERT_BIT_LOW(7, read2);

    TEST_ASSERT_BIT_HIGH(6, read1 ^ read2);

    /* Expire tWC. */
    at28c256_tick(iut, 14900);

    /* Address should still be default unloaded value (0) */
    TEST_ASSERT_EQUAL_UINT8(0x00, at28c256_read(iut, 0x0000));
}

void test_sdp_enable(void)
{
    uint8_t expected;
    uint8_t i;

    /* Ensure an initial write works */
    at28c256_write(iut, 0x1000, 0xAA);
    at28c256_tick(iut, 8);

    at28c256_tick(iut, 15000);

    TEST_ASSERT_EQUAL_UINT8(0xAA, at28c256_read(iut, 0x1000));

    /* Do the SDP enable sequence write. */
    at28c256_write(iut, 0x5555, 0xAA);
    at28c256_tick(iut, 8);

    at28c256_write(iut, 0x2AAA, 0x55);
    at28c256_tick(iut, 8);

    at28c256_write(iut, 0x5555, 0xA0);
    at28c256_tick(iut, 8);

    /* Strobe/Poll should reflect the last write during tWC */
    /* TODO: Does bit 6 invert in the first or second write? */
    expected = 0xA0 ^ 0xC0;

    for(i=0; i<4; ++i)
    {
        TEST_ASSERT_EQUAL_UINT8(expected, at28c256_read(iut, 0x5555));
        at28c256_tick(iut, 10);

        expected ^= 0x40;
    }

    at28c256_tick(iut, 15000);

    /* SDP Should now be enable. Try writing to 0x1000 again. */
    at28c256_write(iut, 0x1000, 0x55);
    at28c256_tick(iut, 15008);

    /* Should have been blocked. */
    TEST_ASSERT_EQUAL_UINT8(0xAA, at28c256_read(iut, 0x1000));
}

void test_sdp_disable(void)
{
    at28c256_set_sdp_enable(iut, true);

    /* Perform the Disable sequence. */
    at28c256_write(iut, 0x5555, 0xAA);
    at28c256_tick(iut, 8);

    at28c256_write(iut, 0x2AAA, 0x55);
    at28c256_tick(iut, 8);

    at28c256_write(iut, 0x5555, 0x80);
    at28c256_tick(iut, 8);

    at28c256_write(iut, 0x5555, 0xAA);
    at28c256_tick(iut, 8);

    at28c256_write(iut, 0x2AAA, 0x55);
    at28c256_tick(iut, 8);

    at28c256_write(iut, 0x5555, 0x20);
    at28c256_tick(iut, 8);

    /* Wait for tWC */
    at28c256_tick(iut, 15000);

    /* Write should now work */
    at28c256_write(iut, 0x1000, 0xAA);
    at28c256_tick(iut, 15008);

    TEST_ASSERT_EQUAL_UINT8(0xAA, at28c256_read(iut, 0x1000));
}

void test_sdp_enable_page_write(void)
{
    uint8_t expected;
    uint8_t i;

    /* Start with SDP Enabled. */
    at28c256_set_sdp_enable(iut, true);

    /* Do the SDP enable sequence write. */
    at28c256_write(iut, 0x5555, 0xAA);
    at28c256_tick(iut, 8);

    at28c256_write(iut, 0x2AAA, 0x55);
    at28c256_tick(iut, 8);

    at28c256_write(iut, 0x5555, 0xA0);
    at28c256_tick(iut, 8);

    /* Write a few bytes to a page now that they should be
     * temporarily enabled. */
    at28c256_write(iut, 0x1000, 0xbe);
    at28c256_tick(iut, 8);
    at28c256_write(iut, 0x1001, 0xef);
    at28c256_tick(iut, 8);

    /* Strobe/Poll should reflect the last write during tWC */
    /* TODO: Does bit 6 invert in the first or second write? */
    expected = 0xef ^ 0xC0;

    for(i=0; i<4; ++i)
    {
        TEST_ASSERT_EQUAL_UINT8(expected, at28c256_read(iut, 0x1001));
        at28c256_tick(iut, 10);

        expected ^= 0x40;
    }

    /* Timeout tWC */
    at28c256_tick(iut, 15000);

    /* Two writes should have taken */
    TEST_ASSERT_EQUAL_UINT8(0xbe, at28c256_read(iut, 0x1000));
    TEST_ASSERT_EQUAL_UINT8(0xef, at28c256_read(iut, 0x1001));
}

int main(int argc, char *argv[])
{
    UNITY_BEGIN();
    RUN_TEST(test_image_load);
    RUN_TEST(test_image_load_offset);
    RUN_TEST(test_image_load_offset_too_large);
    RUN_TEST(test_single_byte_write);
    RUN_TEST(test_partial_page_write);
    RUN_TEST(test_sdp_block);
    RUN_TEST(test_sdp_enable);
    RUN_TEST(test_sdp_disable);
    RUN_TEST(test_sdp_enable_page_write);
    return UNITY_END();
}
