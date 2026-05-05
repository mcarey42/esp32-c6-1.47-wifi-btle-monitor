#include "unity.h"
#include "wlogger_stats.h"
#include <string.h>

void setUp(void) {} void tearDown(void) {}

static detection_t mk(int rssi, uint8_t mac_last) {
    detection_t d = { .type = DET_BLE, .rssi = rssi, .channel = 37 };
    d.mac[5] = mac_last;
    return d;
}

static void test_topn_keeps_strongest(void) {
    stats_t s; stats_init(&s);
    int rssis[] = { -90, -50, -80, -40, -70, -60, -55, -85, -45, -75 };
    for (int i = 0; i < 10; ++i) {
        detection_t d = mk(rssis[i], i);
        stats_update_topn(&s, &d);
    }
    int best = -127;
    for (int i = 0; i < WLOG_TOPN; ++i)
        if (s.strongest[i].rssi > best) best = s.strongest[i].rssi;
    TEST_ASSERT_EQUAL_INT8(-40, best);
    int worst = 0;
    for (int i = 0; i < WLOG_TOPN; ++i)
        if (s.strongest[i].rssi < worst) worst = s.strongest[i].rssi;
    TEST_ASSERT_TRUE(worst > -90);
}

static void test_topn_dedupes_same_mac(void) {
    stats_t s; stats_init(&s);
    detection_t a = mk(-70, 1);
    detection_t b = mk(-50, 1);
    stats_update_topn(&s, &a);
    stats_update_topn(&s, &b);
    int matches = 0;
    for (int i = 0; i < WLOG_TOPN; ++i)
        if (s.strongest[i].mac[5] == 1) ++matches;
    TEST_ASSERT_EQUAL_INT(1, matches);
}

static void test_rate_bucket_increments(void) {
    stats_t s; stats_init(&s);
    s.last_minute_t = 0;
    stats_increment_rate_bucket(&s, 5);
    stats_increment_rate_bucket(&s, 10);
    TEST_ASSERT_EQUAL_UINT16(2, stats_rate_last_minute(&s));

    stats_increment_rate_bucket(&s, 65);
    TEST_ASSERT_EQUAL_UINT16(1, stats_rate_last_minute(&s));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_topn_keeps_strongest);
    RUN_TEST(test_topn_dedupes_same_mac);
    RUN_TEST(test_rate_bucket_increments);
    return UNITY_END();
}
