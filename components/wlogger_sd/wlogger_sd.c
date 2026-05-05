#include "wlogger_sd.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "esp_log.h"
#include <dirent.h>
#include <sys/stat.h>
#include <string.h>
#include <stdio.h>

static const char *TAG = "sd";

#define PIN_CS    4
#define PIN_MISO  5
#define PIN_MOSI  6
#define PIN_SCLK  7
#define HOST_ID   SPI2_HOST

#define ROT_BYTES   (5UL * 1024 * 1024)
#define ROT_SECONDS 3600

esp_err_t wlogger_sd_mount(void) {
    spi_bus_config_t bus = {
        .miso_io_num = PIN_MISO, .mosi_io_num = PIN_MOSI,
        .sclk_io_num = PIN_SCLK, .quadwp_io_num = -1, .quadhd_io_num = -1,
        .max_transfer_sz = 4096,
    };
    esp_err_t err = spi_bus_initialize(HOST_ID, &bus, SDSPI_DEFAULT_DMA);
    if (err != ESP_OK) { ESP_LOGE(TAG, "spi_bus_initialize: %s", esp_err_to_name(err)); return err; }

    sdspi_device_config_t dev = SDSPI_DEVICE_CONFIG_DEFAULT();
    dev.gpio_cs = PIN_CS;
    dev.host_id = HOST_ID;
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = HOST_ID;

    esp_vfs_fat_sdmmc_mount_config_t mcfg = {
        .format_if_mount_failed = false,
        .max_files = 4,
        .allocation_unit_size = 16 * 1024,
    };
    sdmmc_card_t *card;
    err = esp_vfs_fat_sdspi_mount(WLOG_SD_MOUNT, &host, &dev, &mcfg, &card);
    if (err != ESP_OK) { ESP_LOGE(TAG, "mount: %s", esp_err_to_name(err)); return err; }

    mkdir(WLOG_DIR, 0777);
    return ESP_OK;
}

static uint16_t scan_max_session(void) {
    DIR *d = opendir(WLOG_DIR);
    if (!d) return 0;
    uint16_t maxs = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        unsigned int s, i;
        if (sscanf(e->d_name, "log_%u_%u.csv", &s, &i) == 2 && s <= 0xFFFF)
            if ((uint16_t)s > maxs) maxs = (uint16_t)s;
    }
    closedir(d);
    return maxs;
}

esp_err_t wlogger_sd_open_new_file(wlogger_file_t *f, uint32_t now, const char *cycle) {
    bool first = (f->fp == NULL && f->session == 0);
    if (first) f->session = scan_max_session() + 1;
    f->index = first ? 1 : (uint16_t)(f->index + 1);
    f->bytes_written = 0;
    f->opened_at_t_sec = now;
    snprintf(f->path, sizeof f->path, WLOG_DIR "/log_%04u_%04u.csv", f->session, f->index);
    f->fp = fopen(f->path, "w");
    if (!f->fp) return ESP_FAIL;
    int n = fprintf(f->fp,
        "# session=%u start_t_sec=%u fw=wlogger-0.1 cycle=%s\n"
        "t_sec,type,mac,rssi,ch,name,auth,mfg,meta\n",
        f->session, (unsigned)now, cycle ? cycle : "balanced");
    if (n > 0) f->bytes_written += (uint32_t)n;
    return ESP_OK;
}

esp_err_t wlogger_sd_close(wlogger_file_t *f) {
    if (f->fp) { fflush(f->fp); fclose(f->fp); f->fp = NULL; }
    return ESP_OK;
}

bool wlogger_sd_should_rotate(const wlogger_file_t *f, uint32_t now) {
    if (!f->fp) return true;
    if (f->bytes_written >= ROT_BYTES) return true;
    if (now - f->opened_at_t_sec >= ROT_SECONDS) return true;
    return false;
}

uint64_t wlogger_sd_free_bytes(void) {
    FATFS *fs;
    DWORD free_clusters;
    if (f_getfree("0:", &free_clusters, &fs) != FR_OK) return 0;
    return (uint64_t)free_clusters * fs->csize * 512;
}
