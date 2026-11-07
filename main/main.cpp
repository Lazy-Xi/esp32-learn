
#include "esp_log.h"

#include "freertos/FreeRTOS.h" // IWYU pragma: keep
#include "freertos/task.h"

#include "sdkconfig.h"

#include "relay.hpp"
#include "storage/filesystem.hpp"
#include "wifi_manager.hpp"

#include "esp_err.h"

#ifdef CONFIG_WEB_SERVER_ENABLED
#include "web_server.hpp"
#endif

namespace { constexpr char kTag[] = "APP"; } // namespace

// Main
extern "C" void app_main() {
    ESP_LOGI(kTag, "Chemecar starting...");

    // 1. 初始化继电器
    ESP_ERROR_CHECK(Relay::init());
    ESP_ERROR_CHECK(Relay::all_off());
    ESP_LOGI(kTag, "All relays OFF");

    // 2. 初始化 Wi-Fi STA
    ESP_ERROR_CHECK(WiFiManager::init());

    // 3. 初始化 LittleFS 挂载
    ESP_ERROR_CHECK(filesystem_init());

    // 4. 初始化 Web Server
#ifdef CONFIG_WEB_SERVER_ENABLED
    WebServer web_server;
    StaticFilesConfig config {};
    config.spa_fallback = true;
    ESP_ERROR_CHECK(web_server.static_files("/", "/littlefs", config));
    ESP_ERROR_CHECK(web_server.start());
#endif

    // 5. 网络生命周期管理
    for (;;) {
        const EventBits_t bits = WiFiManager::wait();

        // 网络连接成功
        if ((bits & WiFiManager::kOnline) != 0) {
            // 等待断网
            while (WiFiManager::is_online()) {
                vTaskDelay(pdMS_TO_TICKS(250));
            }
        }
    }
}
