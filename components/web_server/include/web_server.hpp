#pragma once

#include <cstdint>

#include "esp_err.h"
#include "esp_http_server.h"

// ============================================================
// 业务回调接口
// ============================================================

struct RelayApi {

    using GetStateCallback = esp_err_t (*)(
        void *context, uint8_t id, bool *state);

    using SetStateCallback = esp_err_t (*)(
        void *context, uint8_t id, bool state);

    uint8_t count = 0;

    GetStateCallback get_state = nullptr;

    SetStateCallback set_state = nullptr;

    void *context = nullptr;
};

// ============================================================
// WebServer
// ============================================================

class WebServer {
public:
    WebServer() = default;

    ~WebServer() = default;

    WebServer(const WebServer &) = delete;

    WebServer &operator=(const WebServer &) = delete;

    // 启动服务
    esp_err_t start(const RelayApi &api);

    // 停止服务
    esp_err_t stop();

    // 服务状态
    bool is_running() const;

    // 获取底层句柄
    httpd_handle_t handle() const;

private:
    // API
    static esp_err_t get_relays(httpd_req_t *req);

    static esp_err_t put_relay(httpd_req_t *req);

    static esp_err_t all_off(httpd_req_t *req);

    // 注册路由
    esp_err_t register_routes();

    // JSON 请求
    static esp_err_t read_json(httpd_req_t *req, char **buffer);

    // JSON 响应
    static esp_err_t send_json(httpd_req_t *req, const char *json);

    // 错误响应
    static esp_err_t send_error(
        httpd_req_t *req, int status, const char *message);

private:
    httpd_handle_t server_ = nullptr;

    RelayApi relay_api_ {};
};
