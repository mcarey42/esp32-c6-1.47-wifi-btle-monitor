#include "unity.h"
#include "wlogger_parsers.h"
#include <string.h>

void setUp(void) {} void tearDown(void) {}

static void test_ssid_present(void) {
    uint8_t tagged[] = {0x00, 0x08, 'h','o','m','e','-','n','e','t', 0x01, 0x00};
    char name[33] = {0};
    parse_ssid(tagged, sizeof tagged, name, sizeof name);
    TEST_ASSERT_EQUAL_STRING("home-net", name);
}

static void test_ssid_hidden_zero_length(void) {
    uint8_t tagged[] = {0x00, 0x00, 0x01, 0x00};
    char name[33] = {0};
    parse_ssid(tagged, sizeof tagged, name, sizeof name);
    TEST_ASSERT_EQUAL_STRING("", name);
}

static void test_ssid_after_other_tag(void) {
    uint8_t tagged[] = {0x05, 0x02, 0xaa, 0xbb,
                        0x00, 0x04, 't','e','s','t',
                        0x03, 0x01, 0x06};
    char name[33] = {0};
    parse_ssid(tagged, sizeof tagged, name, sizeof name);
    TEST_ASSERT_EQUAL_STRING("test", name);
}

static void test_ssid_truncated_length(void) {
    uint8_t tagged[] = {0x00, 0x99, 'a','b','c','d'};
    char name[33] = {0};
    parse_ssid(tagged, sizeof tagged, name, sizeof name);
    TEST_ASSERT_EQUAL_STRING("", name);
}

static void test_ssid_with_control_byte(void) {
    uint8_t tagged[] = {0x00, 0x05, 'a',0x07,'b','c','d'};
    char name[33] = {0};
    parse_ssid(tagged, sizeof tagged, name, sizeof name);
    TEST_ASSERT_EQUAL_STRING("a?bcd", name);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_ssid_present);
    RUN_TEST(test_ssid_hidden_zero_length);
    RUN_TEST(test_ssid_after_other_tag);
    RUN_TEST(test_ssid_truncated_length);
    RUN_TEST(test_ssid_with_control_byte);
    return UNITY_END();
}
