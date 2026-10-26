#include "relay_http_server.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string_view>
#include <strings.h>

#include "cJSON.h"
#include "esp_log.h"

extern const unsigned char index_start[] asm("_binary_index_html_start");
extern const unsigned char index_end[] asm("_binary_index_html_end");

namespace {
constexpr char kTag[] = "relay_http";
constexpr std::size_t kMaxBody = 128;

esp_err_t json(httpd_req_t *request, const char *status, const char *body) {
  httpd_resp_set_status(request, status);
  httpd_resp_set_type(request, "application/json; charset=utf-8");
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");
  return httpd_resp_send(request, body, HTTPD_RESP_USE_STRLEN);
}

esp_err_t error(httpd_req_t *request, const char *status, const char *message,
                bool close = false) {
  char body[160];
  std::snprintf(body, sizeof(body), "{\"error\":\"%s\"}", message);
  if (close)
    httpd_resp_set_hdr(request, "Connection", "close");
  const auto result = json(request, status, body);
  // 有未消费的请求体时关闭连接，避免残留字节成为下一条请求。
  return close ? ESP_FAIL : result;
}

bool is_json(httpd_req_t *request) {
  char content_type[64]{};
  if (httpd_req_get_hdr_value_str(request, "Content-Type", content_type,
                                  sizeof(content_type)) != ESP_OK)
    return false;
  constexpr char expected[] = "application/json";
  constexpr auto length = sizeof(expected) - 1;
  return strncasecmp(content_type, expected, length) == 0 &&
         (content_type[length] == '\0' || content_type[length] == ';');
}
} // namespace

esp_err_t RelayHttpServer::page(httpd_req_t *request) {
  httpd_resp_set_type(request, "text/html; charset=utf-8");
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");
  // EMBED_TXTFILES 自动附加一个 NUL，不把它计入 HTML 响应。
  return httpd_resp_send(request, reinterpret_cast<const char *>(index_start),
                         index_end - index_start - 1);
}

esp_err_t RelayHttpServer::send_state(httpd_req_t *request) {
  const auto &self = *static_cast<RelayHttpServer *>(request->user_ctx);
  const auto state = self.relays_.state();
  char body[384];
  const auto boolean = [](bool value) { return value ? "true" : "false"; };
  std::snprintf(
      body, sizeof(body),
      "{\"online\":%s,\"relays\":["
      "{\"id\":1,\"gpio\":%d,\"on\":%s},{\"id\":2,\"gpio\":%d,\"on\":%s},"
      "{\"id\":3,\"gpio\":%d,\"on\":%s},{\"id\":4,\"gpio\":%d,\"on\":%s}]}",
      boolean(state.online), static_cast<int>(RelayController::pins[0]),
      boolean(state.on[0]), static_cast<int>(RelayController::pins[1]),
      boolean(state.on[1]), static_cast<int>(RelayController::pins[2]),
      boolean(state.on[2]), static_cast<int>(RelayController::pins[3]),
      boolean(state.on[3]));
  return json(request, "200 OK", body);
}

esp_err_t RelayHttpServer::get_relays(httpd_req_t *request) {
  return send_state(request);
}

esp_err_t RelayHttpServer::put_relay(httpd_req_t *request) {
  std::string_view path(request->uri);
  path = path.substr(0, path.find('?'));
  constexpr std::string_view prefix = "/api/relays/";
  if (!path.starts_with(prefix) || path.size() != prefix.size() + 1 ||
      path.back() < '1' || path.back() > '4') {
    return error(request, "404 Not Found", "Relay id must be 1..4", true);
  }
  if (!is_json(request)) {
    return error(request, "415 Unsupported Media Type",
                 "Content-Type must be application/json", true);
  }
  if (request->content_len > kMaxBody) {
    return error(request, "413 Content Too Large", "Body exceeds 128 bytes",
                 true);
  }
  if (request->content_len == 0) {
    return error(request, "400 Bad Request", "Expected a JSON body");
  }

  char body[kMaxBody + 1]{};
  std::size_t received = 0;
  while (received < request->content_len) {
    const int count = httpd_req_recv(request, body + received,
                                     request->content_len - received);
    if (count == HTTPD_SOCK_ERR_TIMEOUT) {
      return error(request, "408 Request Timeout", "Request body timed out",
                   true);
    }
    if (count <= 0)
      return ESP_FAIL;
    received += static_cast<std::size_t>(count);
  }

  // 此 API 只接受扁平对象，先排除嵌套输入，限制递归解析的任务栈开销。
  if (std::count(body, body + received, '{') != 1 ||
      std::find(body, body + received, '[') != body + received) {
    return error(request, "400 Bad Request",
                 "Expected one boolean field named on");
  }
  const char *parse_end = nullptr;
  cJSON *document =
      cJSON_ParseWithLengthOpts(body, received + 1, &parse_end, true);
  const cJSON *value = cJSON_GetObjectItemCaseSensitive(document, "on");
  // 仅接受一个布尔字段；拒绝数字、字符串、重复字段和多余数据。
  const bool valid = cJSON_IsObject(document) && cJSON_IsBool(value) &&
                     cJSON_GetArraySize(document) == 1 &&
                     parse_end == body + received;
  const bool on = cJSON_IsTrue(value);
  cJSON_Delete(document);
  if (!valid) {
    return error(request, "400 Bad Request",
                 "Expected one boolean field named on");
  }

  auto &self = *static_cast<RelayHttpServer *>(request->user_ctx);
  const auto channel = static_cast<std::size_t>(path.back() - '0');
  const auto result = self.relays_.set(channel, on);
  if (result == ESP_ERR_INVALID_STATE) {
    return error(request, "503 Service Unavailable", "WiFi is offline");
  }
  if (result != ESP_OK) {
    return error(request, "500 Internal Server Error", "GPIO update failed");
  }
  ESP_LOGI(kTag, "Relay %u %s", static_cast<unsigned>(channel),
           on ? "ON" : "OFF");
  return send_state(request);
}

esp_err_t RelayHttpServer::all_off(httpd_req_t *request) {
  if (!is_json(request)) {
    return error(request, "415 Unsupported Media Type",
                 "Content-Type must be application/json", true);
  }
  if (request->content_len != 0) {
    return error(request, "400 Bad Request", "This endpoint takes no body",
                 true);
  }
  auto &self = *static_cast<RelayHttpServer *>(request->user_ctx);
  if (self.relays_.all_off() != ESP_OK) {
    return error(request, "500 Internal Server Error", "GPIO update failed");
  }
  return send_state(request);
}

esp_err_t RelayHttpServer::start() {
  if (server_ != nullptr)
    return ESP_OK;
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 80;
  config.uri_match_fn = httpd_uri_match_wildcard;
  config.lru_purge_enable = true;
  config.recv_wait_timeout = 3;
  config.send_wait_timeout = 3;
  const auto result = httpd_start(&server_, &config);
  if (result != ESP_OK) {
    server_ = nullptr;
    return result;
  }

  const struct Route {
    const char *uri;
    httpd_method_t method;
    esp_err_t (*handler)(httpd_req_t *);
  } routes[] = {{"/", HTTP_GET, page},
                {"/api/relays", HTTP_GET, get_relays},
                {"/api/relays/all-off", HTTP_POST, all_off},
                {"/api/relays/*", HTTP_PUT, put_relay}};
  for (const auto &route : routes) {
    httpd_uri_t uri{};
    uri.uri = route.uri;
    uri.method = route.method;
    uri.handler = route.handler;
    uri.user_ctx = this;
    const auto err = httpd_register_uri_handler(server_, &uri);
    if (err != ESP_OK) {
      const auto stop_result = stop();
      if (stop_result != ESP_OK) {
        ESP_LOGE(kTag, "Cleanup failed: %s", esp_err_to_name(stop_result));
      }
      return err;
    }
  }
  ESP_LOGI(kTag, "HTTP server listening on port 80");
  return ESP_OK;
}

esp_err_t RelayHttpServer::stop() {
  if (server_ == nullptr)
    return ESP_OK;
  const auto result = httpd_stop(server_);
  if (result == ESP_OK)
    server_ = nullptr;
  return result;
}
