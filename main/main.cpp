#include <cstdint>
#include <cstring>

#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "relay_controller.hpp"
#include "relay_http_server.hpp"
#include "sdkconfig.h"

namespace {

constexpr char kTag[] = "relay4";
constexpr EventBits_t kOnline = BIT0;
constexpr EventBits_t kFailed = BIT1;
constexpr unsigned kMaxRetries = 10;

#ifdef CONFIG_RELAY_ACTIVE_LOW
constexpr bool kActiveLow = true;
#else
constexpr bool kActiveLow = false;
#endif

RelayController relays;
RelayHttpServer http_server{relays};
EventGroupHandle_t wifi_events = nullptr;
unsigned retries = 0; // 仅由默认事件循环的回调访问。

void wifi_event(void *, esp_event_base_t base, std::int32_t id, void *data) {
  if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
    ESP_ERROR_CHECK(esp_wifi_connect());
  } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
    ESP_ERROR_CHECK(relays.set_online(false));
    xEventGroupClearBits(wifi_events, kOnline);
    const auto *event = static_cast<wifi_event_sta_disconnected_t *>(data);
    ESP_LOGW(kTag, "WiFi disconnected, reason=%u",
             static_cast<unsigned>(event->reason));

    if (retries < kMaxRetries) {
      ++retries;
      ESP_LOGI(kTag, "Reconnect %u/%u", retries, kMaxRetries);
      ESP_ERROR_CHECK(esp_wifi_connect());
    } else {
      xEventGroupSetBits(wifi_events, kFailed);
      ESP_LOGE(kTag, "Retries exhausted; check WiFi and restart");
    }
  } else if (base == IP_EVENT && id == IP_EVENT_STA_LOST_IP) {
    ESP_ERROR_CHECK(relays.set_online(false));
    xEventGroupClearBits(wifi_events, kOnline);
  } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
    const auto *event = static_cast<ip_event_got_ip_t *>(data);
    retries = 0;
    ESP_ERROR_CHECK(relays.set_online(true));
    xEventGroupClearBits(wifi_events, kFailed);
    xEventGroupSetBits(wifi_events, kOnline);
    ESP_LOGI(kTag, "WiFi ready, IP=" IPSTR, IP2STR(&event->ip_info.ip));
#ifdef CONFIG_RELAY_HTTP_ENABLED
    ESP_LOGI(kTag, "Open http://" IPSTR "/", IP2STR(&event->ip_info.ip));
#endif
  }
}

void start_wifi() {
  // 此恢复分支会清空本练习工程的整个默认 NVS 分区。
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
      err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  ESP_ERROR_CHECK(err);

  wifi_events = xEventGroupCreate();
  if (wifi_events == nullptr) {
    ESP_LOGE(kTag, "Cannot allocate WiFi event group");
    ESP_ERROR_CHECK(ESP_ERR_NO_MEM);
  }

  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  if (esp_netif_create_default_wifi_sta() == nullptr) {
    ESP_ERROR_CHECK(ESP_ERR_NO_MEM);
  }

  wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&init_config));
  ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                             &wifi_event, nullptr));
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                             &wifi_event, nullptr));
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_LOST_IP,
                                             &wifi_event, nullptr));

  constexpr char ssid[] = CONFIG_RELAY_WIFI_SSID;
  constexpr char password[] = CONFIG_RELAY_WIFI_PASSWORD;
  static_assert(sizeof(ssid) > 1 && sizeof(ssid) - 1 <= 32,
                "SSID must contain 1..32 bytes");
  static_assert(sizeof(password) - 1 >= 8 && sizeof(password) - 1 <= 63,
                "This example requires an 8..63 byte passphrase");

  wifi_config_t config{};
  std::memcpy(config.sta.ssid, ssid, sizeof(ssid) - 1);
  std::memcpy(config.sta.password, password, sizeof(password) - 1);
  config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
  config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;

  ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &config));
  ESP_ERROR_CHECK(esp_wifi_start());
}

#ifdef CONFIG_RELAY_RUN_DEMO
// 每 100 ms 检查一次连接状态，让演示在失去联网标志后尽快停止。
bool wait_while_online(unsigned milliseconds) {
  for (unsigned elapsed = 0; elapsed < milliseconds; elapsed += 100) {
    if ((xEventGroupGetBits(wifi_events) & kOnline) == 0) {
      return false;
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
  return (xEventGroupGetBits(wifi_events) & kOnline) != 0;
}
#endif

} // namespace

extern "C" void app_main() {
  // 继电器先关闭，再初始化联网；连接失败不会阻塞关闭操作。
  ESP_ERROR_CHECK(relays.init());
  ESP_LOGI(kTag, "All relays OFF; active-low=%d", kActiveLow ? 1 : 0);
  start_wifi();

  for (;;) {
    ESP_ERROR_CHECK(relays.all_off());
    const auto bits = xEventGroupWaitBits(wifi_events, kOnline | kFailed,
                                          pdFALSE, pdFALSE, portMAX_DELAY);
    if ((bits & kFailed) != 0) {
      ESP_ERROR_CHECK(http_server.stop());
      ESP_ERROR_CHECK(relays.all_off());
      ESP_LOGE(kTag, "Relays OFF. Restart after fixing WiFi settings.");
      return;
    }

#ifdef CONFIG_RELAY_HTTP_ENABLED
    ESP_ERROR_CHECK(http_server.start());
#endif

#ifdef CONFIG_RELAY_RUN_DEMO
    for (std::size_t channel = 1; channel <= 4; ++channel) {
      if ((xEventGroupGetBits(wifi_events) & kOnline) == 0) {
        break;
      }
      ESP_LOGI(kTag, "Relay %u ON", static_cast<unsigned>(channel));
      const auto err = relays.set(channel, true);
      if (err == ESP_ERR_INVALID_STATE)
        break;
      ESP_ERROR_CHECK(err);
      const bool still_online = wait_while_online(2000);
      // 断线回调也会释放通道；已离线时不再提交控制指令。
      const auto off_err = relays.set(channel, false);
      if (off_err != ESP_ERR_INVALID_STATE)
        ESP_ERROR_CHECK(off_err);
      ESP_LOGI(kTag, "Relay %u OFF", static_cast<unsigned>(channel));
      if (!still_online || !wait_while_online(500)) {
        break;
      }
    }
    ESP_ERROR_CHECK(relays.all_off());
    (void)wait_while_online(2000);
#else
    ESP_LOGI(kTag, "WiFi connected; waiting for relay commands");
    while ((xEventGroupGetBits(wifi_events) & kOnline) != 0) {
      vTaskDelay(pdMS_TO_TICKS(250));
    }
#ifdef CONFIG_RELAY_HTTP_ENABLED
    ESP_ERROR_CHECK(http_server.stop());
#endif
#endif
  }
}
