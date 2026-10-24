/* WiFi station Example

This example code is in the Public Domain (or CC0 licensed, at your option.)

Unless required by applicable law or agreed to in writing, this
software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
CONDITIONS OF ANY KIND, either express or implied.
*/
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include <cstring>

namespace {

/* The examples use WiFi configuration that you can set via project
configuration menu

If you'd rather not, just change the below entries to strings with
the config you want - ie #define EXAMPLE_WIFI_SSID "mywifissid"
*/
#define EXAMPLE_ESP_WIFI_SSID CONFIG_ESP_WIFI_SSID
#define EXAMPLE_ESP_WIFI_PASS CONFIG_ESP_WIFI_PASSWORD
#define EXAMPLE_ESP_MAXIMUM_RETRY CONFIG_ESP_MAXIMUM_RETRY

#if CONFIG_ESP_STATION_EXAMPLE_WPA3_SAE_PWE_HUNT_AND_PECK
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_HUNT_AND_PECK
#define EXAMPLE_H2E_IDENTIFIER ""
#elif CONFIG_ESP_STATION_EXAMPLE_WPA3_SAE_PWE_HASH_TO_ELEMENT
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_HASH_TO_ELEMENT
#define EXAMPLE_H2E_IDENTIFIER CONFIG_ESP_WIFI_PW_ID
#elif CONFIG_ESP_STATION_EXAMPLE_WPA3_SAE_PWE_BOTH
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_BOTH
#define EXAMPLE_H2E_IDENTIFIER CONFIG_ESP_WIFI_PW_ID
#endif
#if CONFIG_ESP_WIFI_AUTH_OPEN
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_OPEN
#elif CONFIG_ESP_WIFI_AUTH_WEP
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WEP
#elif CONFIG_ESP_WIFI_AUTH_WPA_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA3_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_WPA3_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WAPI_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WAPI_PSK
#endif

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about
 * two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

constexpr auto TAG = "wifi station";

static int s_retry_num = 0;
static httpd_handle_t s_http_server = nullptr;

/* CMake embeds index.html into the firmware, including a trailing NUL. */
extern const char index_html_start[] asm("_binary_index_html_start");
extern const char index_html_end[] asm("_binary_index_html_end");

static esp_err_t root_get_handler(httpd_req_t *req) {
  esp_err_t ret = httpd_resp_set_type(req, "text/html; charset=utf-8");
  if (ret != ESP_OK) {
    return ret;
  }
  return httpd_resp_send(req, index_html_start,
                         index_html_end - index_html_start - 1);
}

static void start_http_server(void) {
  if (s_http_server != nullptr) {
    return;
  }

  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 80;
  esp_err_t ret = httpd_start(&s_http_server, &config);
  if (ret != ESP_OK) {
    s_http_server = nullptr;
    ESP_LOGE(TAG, "HTTP server start failed: %s", esp_err_to_name(ret));
    return;
  }

  httpd_uri_t root{};
  root.uri = "/";
  root.method = HTTP_GET;
  root.handler = root_get_handler;
  ret = httpd_register_uri_handler(s_http_server, &root);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Registering / failed: %s", esp_err_to_name(ret));
    ESP_ERROR_CHECK(httpd_stop(s_http_server));
    s_http_server = nullptr;
  }
}

static void stop_http_server(void) {
  if (s_http_server != nullptr) {
    ESP_ERROR_CHECK(httpd_stop(s_http_server));
    s_http_server = nullptr;
    ESP_LOGI(TAG, "HTTP server stopped");
  }
}

static void event_handler(void *, esp_event_base_t event_base, int32_t event_id,
                          void *event_data) {
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
  } else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_STA_DISCONNECTED) {
    const auto *event =
        static_cast<const wifi_event_sta_disconnected_t *>(event_data);
    ESP_LOGW(TAG, "Disconnected from AP, reason=%u",
             static_cast<unsigned>(event->reason));
    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    stop_http_server();
    if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY) {
      esp_wifi_connect();
      s_retry_num++;
      ESP_LOGI(TAG, "Retrying AP connection (%d/%d)", s_retry_num,
               EXAMPLE_ESP_MAXIMUM_RETRY);
    } else {
      ESP_LOGE(TAG, "AP connection retries exhausted");
      xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
    }
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    const auto *event = static_cast<const ip_event_got_ip_t *>(event_data);
    ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
    s_retry_num = 0;
    xEventGroupClearBits(s_wifi_event_group, WIFI_FAIL_BIT);
    start_http_server();
    if (s_http_server != nullptr) {
      ESP_LOGI(TAG, "Open http://" IPSTR "/ in your browser",
               IP2STR(&event->ip_info.ip));
    }
    xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
  }
}

void wifi_init_sta(void) {
  s_wifi_event_group = xEventGroupCreate();
  configASSERT(s_wifi_event_group != nullptr);

  ESP_ERROR_CHECK(esp_netif_init());

  ESP_ERROR_CHECK(esp_event_loop_create_default());
  esp_netif_create_default_wifi_sta();

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  esp_event_handler_instance_t instance_any_id;
  esp_event_handler_instance_t instance_got_ip;
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, nullptr, &instance_any_id));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, nullptr,
      &instance_got_ip));

  wifi_config_t wifi_config{};
  constexpr char ssid[] = EXAMPLE_ESP_WIFI_SSID;
  constexpr char password[] = EXAMPLE_ESP_WIFI_PASS;
  constexpr char h2e_identifier[] = EXAMPLE_H2E_IDENTIFIER;
  static_assert(sizeof(ssid) - 1 <= sizeof(wifi_config.sta.ssid),
                "Wi-Fi SSID must be at most 32 bytes");
  static_assert(sizeof(password) - 1 <= sizeof(wifi_config.sta.password),
                "Wi-Fi password must be at most 64 bytes");
  static_assert(sizeof(h2e_identifier) <=
                    sizeof(wifi_config.sta.sae_h2e_identifier),
                "WPA3 password identifier is too long");
  std::memcpy(wifi_config.sta.ssid, ssid, sizeof(ssid) - 1);
  std::memcpy(wifi_config.sta.password, password, sizeof(password) - 1);
  wifi_config.sta.threshold.authmode = ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD;
  wifi_config.sta.sae_pwe_h2e = ESP_WIFI_SAE_MODE;
  std::memcpy(wifi_config.sta.sae_h2e_identifier, h2e_identifier,
              sizeof(h2e_identifier));
#ifdef CONFIG_ESP_WIFI_WPA3_COMPATIBLE_SUPPORT
  wifi_config.sta.disable_wpa3_compatible_mode = 0;
#endif
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
  ESP_ERROR_CHECK(esp_wifi_start());

  ESP_LOGI(TAG, "wifi_init_sta finished.");

  /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or
   * connection failed for the maximum number of re-tries (WIFI_FAIL_BIT). The
   * bits are set by event_handler() (see above) */
  EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                         WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                         pdFALSE, pdFALSE, portMAX_DELAY);

  /* xEventGroupWaitBits() returns the bits before the call returned, hence we
   * can test which event actually happened. */
  if (bits & WIFI_CONNECTED_BIT) {
    ESP_LOGI(TAG, "connected to ap SSID:%s", EXAMPLE_ESP_WIFI_SSID);
  } else if (bits & WIFI_FAIL_BIT) {
    ESP_LOGE(TAG,
             "Failed to connect to SSID:%s; check Wi-Fi settings and restart",
             EXAMPLE_ESP_WIFI_SSID);
  } else {
    ESP_LOGE(TAG, "UNEXPECTED EVENT");
  }
}

} // namespace

extern "C" void app_main(void) {
  // Initialize NVS
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  // Suppress driver diagnostics; application connection logs use a separate
  // tag.
  esp_log_level_set("wifi", ESP_LOG_ERROR);

  ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
  wifi_init_sta();
}
