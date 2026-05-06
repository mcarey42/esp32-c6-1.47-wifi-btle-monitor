#include "wlogger_writer.h"
#include "wlogger_csv.h"
#include "wlogger_sd.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdbool.h>

static const char *TAG = "writer";

static QueueHandle_t  s_q;
static stats_t       *s_stats;
static recent_q_t    *s_recent;
static wlogger_file_t s_file = {0};
static volatile bool  s_rotate_req = false;

void recent_q_init(recent_q_t *r) {
    r->head = 0; r->count = 0;
    r->mtx = xSemaphoreCreateMutex();
}
void recent_q_push(recent_q_t *r, const detection_t *d) {
    xSemaphoreTake(r->mtx, portMAX_DELAY);
    r->entries[r->head] = *d;
    r->head = (r->head + 1) % RECENT_SIZE;
    if (r->count < RECENT_SIZE) r->count++;
    xSemaphoreGive(r->mtx);
}
int recent_q_snapshot(const recent_q_t *r, detection_t *out, int max) {
    xSemaphoreTake(r->mtx, portMAX_DELAY);
    int n = r->count < max ? r->count : max;
    int start = (r->head - n + RECENT_SIZE) % RECENT_SIZE;
    for (int i = 0; i < n; ++i) out[i] = r->entries[(start + i) % RECENT_SIZE];
    xSemaphoreGive(r->mtx);
    return n;
}

esp_err_t wlogger_writer_init(QueueHandle_t q, stats_t *st, recent_q_t *r) {
    s_q = q; s_stats = st; s_recent = r;
    return ESP_OK;
}

void wlogger_writer_request_rotate(void) { s_rotate_req = true; }

static void writer_task(void *_) {
    (void)_;
    detection_t d;
    char buf[4096];
    size_t buf_len = 0;
    TickType_t last_flush = xTaskGetTickCount();

    if (wlogger_sd_open_new_file(&s_file, 0, "balanced") != ESP_OK) {
        ESP_LOGE(TAG, "initial file open failed; writer will idle");
    }

    for (;;) {
        if (xQueueReceive(s_q, &d, pdMS_TO_TICKS(200)) == pdTRUE) {
            int n = wlogger_csv_format(buf + buf_len, sizeof buf - buf_len, &d);
            if (n > 0) buf_len += (size_t)n;
            stats_record_event(s_stats, &d);
            recent_q_push(s_recent, &d);
        }

        bool full  = buf_len > sizeof(buf) - 256;
        bool stale = (xTaskGetTickCount() - last_flush) > pdMS_TO_TICKS(1000);
        if ((full || stale) && s_file.fp && buf_len > 0) {
            size_t w = fwrite(buf, 1, buf_len, s_file.fp);
            fflush(s_file.fp);
            s_file.bytes_written += (uint32_t)w;
            buf_len = 0;
            last_flush = xTaskGetTickCount();

            xSemaphoreTake(s_stats->mtx, portMAX_DELAY);
            strncpy(s_stats->current_file, s_file.path, sizeof s_stats->current_file - 1);
            s_stats->current_file_bytes = s_file.bytes_written;
            s_stats->sd_free_bytes = wlogger_sd_free_bytes();
            xSemaphoreGive(s_stats->mtx);
        }

        bool need = s_rotate_req || wlogger_sd_should_rotate(&s_file,
            (uint32_t)(esp_timer_get_time() / 1000000));
        if (need) {
            if (s_file.fp) wlogger_sd_close(&s_file);
            wlogger_sd_open_new_file(&s_file,
                (uint32_t)(esp_timer_get_time() / 1000000), "balanced");
            s_rotate_req = false;
        }
    }
}

esp_err_t wlogger_writer_start_task(void) {
    // 8 KB stack — writer_task has a 4 KB local CSV buffer plus FreeRTOS/log frame overhead.
    BaseType_t r = xTaskCreate(writer_task, "writer", 8192, NULL, 4, NULL);
    return r == pdPASS ? ESP_OK : ESP_FAIL;
}
