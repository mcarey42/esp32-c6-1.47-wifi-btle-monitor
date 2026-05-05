#pragma once
#include <stdio.h>
#define ESP_LOGE(tag, fmt, ...) fprintf(stderr, "E " tag ": " fmt "\n", ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) fprintf(stderr, "W " tag ": " fmt "\n", ##__VA_ARGS__)
#define ESP_LOGI(tag, fmt, ...) fprintf(stderr, "I " tag ": " fmt "\n", ##__VA_ARGS__)
#define ESP_LOGD(tag, fmt, ...) (void)0
