#include "wifi_manager.hpp"

#include <cstdint>
#include <cstring>

#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"

#include "nvs_flash.h"
#include "sdkconfig.h"

namespace WiFiManager {

namespace {

constexpr char kTag[] = "WiFiManager";
constexpr unsigned kMaxRetries = 10;

EventGroupHandle_t wifi_events = nullptr;

// 仅由默认事件循环任务访问
unsigned retries = 0;

// Wi-Fi 事件处理
void wifi_event(void *arg, esp_event_base_t base, std::int32_t id, void *data) {
    (void) arg;

    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {

        ESP_LOGI(kTag, "WiFi STA started");

        esp_err_t err = esp_wifi_connect();

        if (err != ESP_OK) {
            ESP_LOGW(kTag, "Connect request failed: %s", esp_err_to_name(err));
        }

        return;
    }

    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {

        const auto *event =
            static_cast<const wifi_event_sta_disconnected_t *>(data);

        xEventGroupClearBits(wifi_events, kOnline);

        ESP_LOGW(kTag, "WiFi disconnected, reason=%u",
            event ? static_cast<unsigned>(event->reason) : 0U);

        if (retries < kMaxRetries) {

            ++retries;

            ESP_LOGI(kTag, "Reconnect %u/%u", retries, kMaxRetries);

            esp_err_t err = esp_wifi_connect();

            if (err != ESP_OK) {
                ESP_LOGW(
                    kTag, "Reconnect request failed: %s", esp_err_to_name(err));
            }
        }
        else {

            xEventGroupSetBits(wifi_events, kFailed);

            ESP_LOGE(kTag, "Reconnect retries exhausted");
        }

        return;
    }

    if (base == IP_EVENT && id == IP_EVENT_STA_LOST_IP) {

        xEventGroupClearBits(wifi_events, kOnline);

        ESP_LOGW(kTag, "IP address lost");

        return;
    }

    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {

        const auto *event = static_cast<const ip_event_got_ip_t *>(data);

        retries = 0;

        xEventGroupClearBits(wifi_events, kFailed);
        xEventGroupSetBits(wifi_events, kOnline);

        ESP_LOGI(kTag, "WiFi connected, IP=" IPSTR, IP2STR(&event->ip_info.ip));
    }
}

// NVS 初始化
esp_err_t init_nvs() {
    esp_err_t err = nvs_flash_init();

    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {

        ESP_LOGW(kTag, "NVS recovery required; erasing default NVS partition");

        err = nvs_flash_erase();

        if (err != ESP_OK) {
            return err;
        }

        err = nvs_flash_init();
    }

    return err;
}

// STA 配置
esp_err_t configure_sta() {
    constexpr char ssid[] = CONFIG_WIFI_SSID;
    constexpr char password[] = CONFIG_WIFI_PASSWORD;

    static_assert(sizeof(ssid) > 1 && sizeof(ssid) - 1 <= 32,
        "SSID must contain 1..32 bytes");

    static_assert(sizeof(password) - 1 >= 8 && sizeof(password) - 1 <= 63,
        "Password must contain 8..63 bytes");

    wifi_config_t config {};

    std::memcpy(config.sta.ssid, ssid, sizeof(ssid) - 1);

    std::memcpy(config.sta.password, password, sizeof(password) - 1);

    config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;

    esp_err_t err = esp_wifi_set_storage(WIFI_STORAGE_RAM);

    if (err != ESP_OK) return err;

    err = esp_wifi_set_mode(WIFI_MODE_STA);

    if (err != ESP_OK) return err;

    return esp_wifi_set_config(WIFI_IF_STA, &config);
}

} // namespace

// 初始化
esp_err_t init() {
    if (wifi_events != nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    // 1. NVS
    esp_err_t err = init_nvs();

    if (err != ESP_OK) return err;

    // 2. EventGroup
    wifi_events = xEventGroupCreate();

    if (wifi_events == nullptr) {
        return ESP_ERR_NO_MEM;
    }

    // 3. Netif
    err = esp_netif_init();

    if (err != ESP_OK) return err;

    // 4. 默认事件循环
    err = esp_event_loop_create_default();

    if (err != ESP_OK) return err;

    // 5. STA 网络接口
    if (esp_netif_create_default_wifi_sta() == nullptr) {
        return ESP_ERR_NO_MEM;
    }

    // 6. Wi-Fi 驱动
    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();

    err = esp_wifi_init(&init_config);

    if (err != ESP_OK) return err;

    // 7. Wi-Fi 事件
    err = esp_event_handler_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event, nullptr);

    if (err != ESP_OK) return err;

    // 8. IP 事件
    err = esp_event_handler_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event, nullptr);

    if (err != ESP_OK) return err;

    err = esp_event_handler_register(
        IP_EVENT, IP_EVENT_STA_LOST_IP, &wifi_event, nullptr);

    if (err != ESP_OK) return err;

    // 9. 配置 STA
    err = configure_sta();

    if (err != ESP_OK) return err;

    // 10. 启动
    err = esp_wifi_start();

    if (err != ESP_OK) return err;

    ESP_LOGI(kTag, "WiFi initialized, SSID=%s", CONFIG_WIFI_SSID);

    return ESP_OK;
}

// 等待联网
EventBits_t wait() {
    if (wifi_events == nullptr) {
        return kFailed;
    }

    return xEventGroupWaitBits(
        wifi_events, kOnline | kFailed, pdFALSE, pdFALSE, portMAX_DELAY);
}

// 在线状态
bool is_online() {
    if (wifi_events == nullptr) {
        return false;
    }

    return (xEventGroupGetBits(wifi_events) & kOnline) != 0;
}

} // namespace WiFiManager
