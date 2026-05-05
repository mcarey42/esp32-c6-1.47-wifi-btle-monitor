#include "unity.h"
#include "wlogger_bloom.h"
#include <stdint.h>
#include <string.h>

void setUp(void) {} void tearDown(void) {}

static void mk_mac(uint8_t mac[6], uint32_t i) {
    mac[0]=0x02; mac[1]=(i>>24)&0xff; mac[2]=(i>>16)&0xff;
    mac[3]=(i>>8)&0xff;  mac[4]=i&0xff; mac[5]=0;
}

static void test_inserts_then_contains_all(void) {
    bloom_t b; bloom_init(&b);
    uint8_t mac[6];
    for (uint32_t i = 0; i < 1000; ++i) {
        mk_mac(mac, i);
        bloom_add(&b, mac);
    }
    int hits = 0;
    for (uint32_t i = 0; i < 1000; ++i) {
        mk_mac(mac, i);
        if (bloom_contains(&b, mac)) ++hits;
    }
    TEST_ASSERT_EQUAL_INT(1000, hits);
}

static void test_false_positive_rate_under_2_pct(void) {
    bloom_t b; bloom_init(&b);
    uint8_t mac[6];
    for (uint32_t i = 0; i < 5000; ++i) { mk_mac(mac, i); bloom_add(&b, mac); }
    int fp = 0;
    for (uint32_t i = 5000; i < 15000; ++i) {
        mk_mac(mac, i);
        if (bloom_contains(&b, mac)) ++fp;
    }
    TEST_ASSERT_TRUE(fp < 200);
}

static void test_count_estimate_within_5_pct(void) {
    bloom_t b; bloom_init(&b);
    uint8_t mac[6];
    for (uint32_t i = 0; i < 2000; ++i) { mk_mac(mac, i); bloom_add(&b, mac); }
    uint32_t est = bloom_count_estimate(&b);
    TEST_ASSERT_TRUE(est >= 1900 && est <= 2100);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_inserts_then_contains_all);
    RUN_TEST(test_false_positive_rate_under_2_pct);
    RUN_TEST(test_count_estimate_within_5_pct);
    return UNITY_END();
}
