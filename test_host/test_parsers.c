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

static void test_adv_complete_local_name(void) {
    uint8_t adv[] = {0x0C, 0x09, 'A','i','r','P','o','d','s',' ','P','r','o', 0x02, 0x01, 0x06};
    char name[33] = {0}; uint16_t mfg = 0xFFFF;
    parse_adv_data(adv, sizeof adv, name, sizeof name, &mfg);
    TEST_ASSERT_EQUAL_STRING("AirPods Pro", name);
    TEST_ASSERT_EQUAL_UINT16(0, mfg);
}

static void test_adv_short_local_name_only(void) {
    uint8_t adv[] = {0x05, 0x08, 'M','i','B','d'};
    char name[33] = {0}; uint16_t mfg = 0;
    parse_adv_data(adv, sizeof adv, name, sizeof name, &mfg);
    TEST_ASSERT_EQUAL_STRING("MiBd", name);
}

static void test_adv_mfg_data_apple(void) {
    uint8_t adv[] = {0x05, 0xFF, 0x4C, 0x00, 0x12, 0x34};
    char name[33] = {0}; uint16_t mfg = 0;
    parse_adv_data(adv, sizeof adv, name, sizeof name, &mfg);
    TEST_ASSERT_EQUAL_UINT16(0x004C, mfg);
}

static void test_adv_truncated(void) {
    uint8_t adv[] = {0x07, 0x09, 'B','i'};
    char name[33] = {'x',0};
    parse_adv_data(adv, sizeof adv, name, sizeof name, NULL);
    TEST_ASSERT_EQUAL_STRING("", name);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_ssid_present);
    RUN_TEST(test_ssid_hidden_zero_length);
    RUN_TEST(test_ssid_after_other_tag);
    RUN_TEST(test_ssid_truncated_length);
    RUN_TEST(test_ssid_with_control_byte);
    RUN_TEST(test_adv_complete_local_name);
    RUN_TEST(test_adv_short_local_name_only);
    RUN_TEST(test_adv_mfg_data_apple);
    RUN_TEST(test_adv_truncated);
    return UNITY_END();
}
