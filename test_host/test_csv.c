#include "unity.h"
#include "wlogger_csv.h"
#include <string.h>

void setUp(void) {} void tearDown(void) {}

static void test_wifi_ap_basic(void) {
    detection_t d = { .t_sec=12, .type=DET_WIFI_AP, .rssi=-48, .channel=6,
        .mac={0xa4,0xc3,0xf7,0x11,0x0e,0x22}, .auth=3 /* WPA2 */ };
    strcpy(d.name, "home-net");
    char buf[256];
    int n = wlogger_csv_format(buf, sizeof buf, &d);
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_STRING(
        "12,W,a4c3f7110e22,-48,6,\"home-net\",WPA2,,\n", buf);
}

static void test_probe_random_mac(void) {
    detection_t d = { .t_sec=13, .type=DET_WIFI_PROBE, .rssi=-71, .channel=11,
        .mac={0x3e,0x2a,0x8d,0x7e,0x00,0x33}, .mac_random=true };
    char buf[256];
    int n = wlogger_csv_format(buf, sizeof buf, &d);
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_STRING(
        "13,P,3e2a8d7e0033,-71,11,\"\",,,R\n", buf);
}

static void test_ble_with_mfg(void) {
    detection_t d = { .t_sec=13, .type=DET_BLE, .rssi=-62, .channel=38,
        .mac={0xf8,0x7b,0x5c,0x20,0x4c,0x4d}, .auth=0, .mfg_id=0x004C };
    strcpy(d.name, "AirPods Pro");
    char buf[256];
    int n = wlogger_csv_format(buf, sizeof buf, &d);
    (void)n;
    TEST_ASSERT_EQUAL_STRING(
        "13,B,f87b5c204c4d,-62,38,\"AirPods Pro\",public,004C,\n", buf);
}

static void test_name_with_quote_and_comma(void) {
    detection_t d = { .t_sec=20, .type=DET_BLE, .rssi=-70, .channel=37,
        .mac={1,2,3,4,5,6}, .auth=1 };
    strcpy(d.name, "He said \"hi, there\"");
    char buf[256];
    int n = wlogger_csv_format(buf, sizeof buf, &d);
    (void)n;
    TEST_ASSERT_EQUAL_STRING(
        "20,B,010203040506,-70,37,\"He said \"\"hi, there\"\"\",random,,\n", buf);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_wifi_ap_basic);
    RUN_TEST(test_probe_random_mac);
    RUN_TEST(test_ble_with_mfg);
    RUN_TEST(test_name_with_quote_and_comma);
    return UNITY_END();
}
