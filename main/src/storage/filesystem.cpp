#include "storage/filesystem.hpp"

#include "esp_littlefs.h"
#include "esp_log.h"

static const char *TAG = "filesystem";

esp_err_t filesystem_init() {
    esp_vfs_littlefs_conf_t conf {};

    conf.base_path = "/littlefs";
    conf.partition_label = "littlefs";
    conf.format_if_mount_failed = false;
    conf.dont_mount = false;
    conf.grow_on_mount = false;
    conf.read_only = false;

    esp_err_t ret = esp_vfs_littlefs_register(&conf);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LittleFS mount failed: %s", esp_err_to_name(ret));
        return ret;
    }

    size_t total = 0;
    size_t used = 0;

    ret = esp_littlefs_info("littlefs", &total, &used);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "LittleFS mounted");
        ESP_LOGI(TAG, "Total: %u KB", (unsigned) (total / 1024));
        ESP_LOGI(TAG, "Used: %u KB", (unsigned) (used / 1024));
        ESP_LOGI(TAG, "Free: %u KB", (unsigned) ((total - used) / 1024));
    }

    return ret;
}