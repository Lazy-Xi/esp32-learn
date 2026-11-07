
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "sdkconfig.h"

#include "relay.hpp"
#include "wifi_manager.hpp"

#include "esp_err.h"

#ifdef CONFIG_WEB_SERVER_ENABLED
#include "web_server.hpp"
#endif

namespace {

constexpr char kTag[] = "APP";

#ifdef CONFIG_WEB_SERVER_ENABLED

WebServer web_server;

// ============================================================
// 继电器业务回调
// ============================================================

esp_err_t relay_get_callback(void *context, uint8_t id, bool *state) {
    (void) context;

    return Relay::get_state(id, state);
}

esp_err_t relay_set_callback(void *context, uint8_t id, bool state) {
    (void) context;

    return Relay::set_state(id, state);
}

// ============================================================
// 创建 API
// ============================================================

RelayApi create_relay_api() {
    RelayApi api {};

    api.count = Relay::count();

    api.get_state = relay_get_callback;

    api.set_state = relay_set_callback;

    api.context = nullptr;

    return api;
}

#endif

} // namespace

// ============================================================
// Main
// ============================================================

extern "C" void app_main() {
    ESP_LOGI(kTag, "Chemecar starting...");

    // 1. 初始化继电器
    ESP_ERROR_CHECK(Relay::init());

    ESP_ERROR_CHECK(Relay::all_off());

    ESP_LOGI(kTag, "All relays OFF");

    // 2. 初始化 Wi-Fi STA
    ESP_ERROR_CHECK(WiFiManager::init());

#ifdef CONFIG_WEB_SERVER_ENABLED
    // 3. 创建继电器 API
    const RelayApi relay_api = create_relay_api();
#endif

    // 4. 网络生命周期管理
    for (;;) {

        const EventBits_t bits = WiFiManager::wait();

        // 重连失败
        if ((bits & WiFiManager::kFailed) != 0) {

            ESP_LOGE(kTag, "WiFi reconnect retries exhausted");

#ifdef CONFIG_WEB_SERVER_ENABLED
            if (web_server.is_running()) {
                ESP_ERROR_CHECK(web_server.stop());
            }
#endif

            ESP_ERROR_CHECK(Relay::all_off());

            ESP_LOGE(
                kTag, "All relays OFF. Restart after fixing WiFi settings.");

            return;
        }

        // 网络连接成功
        if ((bits & WiFiManager::kOnline) != 0) {

            ESP_LOGI(kTag, "WiFi connected");

#ifdef CONFIG_WEB_SERVER_ENABLED

            // 启动 HTTP 服务
            if (!web_server.is_running()) {
                ESP_ERROR_CHECK(web_server.start(relay_api));

                ESP_LOGI(kTag, "HTTP server started");
            }

#endif

            ESP_LOGI(kTag, "Waiting for WiFi disconnection...");

            // 等待断网
            while (WiFiManager::is_online()) {
                vTaskDelay(pdMS_TO_TICKS(250));
            }

            ESP_LOGW(kTag, "WiFi disconnected");

#ifdef CONFIG_WEB_SERVER_ENABLED

            // 停止 HTTP 服务
            if (web_server.is_running()) {
                ESP_ERROR_CHECK(web_server.stop());
            }

#endif

            // 断网后关闭全部继电器
            ESP_ERROR_CHECK(Relay::all_off());

            ESP_LOGI(kTag, "Relays OFF; waiting for reconnection");
        }
    }
}
