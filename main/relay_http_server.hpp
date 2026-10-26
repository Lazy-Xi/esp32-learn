#pragma once

#include "esp_http_server.h"
#include "relay_controller.hpp"

// start/stop 只由 app_main 所在任务调用；请求在 HTTP 服务任务中处理。
class RelayHttpServer {
public:
  explicit RelayHttpServer(RelayController &relays) : relays_(relays) {}
  RelayHttpServer(const RelayHttpServer &) = delete;
  RelayHttpServer &operator=(const RelayHttpServer &) = delete;
  esp_err_t start();
  esp_err_t stop();

private:
  static esp_err_t page(httpd_req_t *request);
  static esp_err_t get_relays(httpd_req_t *request);
  static esp_err_t put_relay(httpd_req_t *request);
  static esp_err_t all_off(httpd_req_t *request);
  static esp_err_t send_state(httpd_req_t *request);
  RelayController &relays_;
  httpd_handle_t server_ = nullptr;
};
