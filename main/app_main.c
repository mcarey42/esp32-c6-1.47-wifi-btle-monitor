#include "esp_log.h"
#include "nvs_flash.h"

static const char *TAG = "wlogger";

void app_main(void) {
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_LOGI(TAG, "wlogger boot — skeleton stage");
}
