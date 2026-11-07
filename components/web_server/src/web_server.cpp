#include "web_server.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "cJSON.h"
#include "esp_log.h"
#include "sdkconfig.h"

namespace { constexpr char kTag[] = "WebServer"; }

// ============================================================
// 启动
// ============================================================

esp_err_t WebServer::start(const RelayApi &api) {
    if (server_ != nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    if (api.count == 0 || api.get_state == nullptr ||
        api.set_state == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    relay_api_ = api;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    config.server_port = CONFIG_WEB_SERVER_PORT;
    config.max_open_sockets = CONFIG_WEB_SERVER_MAX_CONNECTIONS;
    config.max_uri_handlers = 8;
    config.stack_size = 6144;
    config.lru_purge_enable = true;

    esp_err_t err = httpd_start(&server_, &config);

    if (err != ESP_OK) {
        server_ = nullptr;
        ESP_LOGE(kTag, "Start failed: %s", esp_err_to_name(err));
        return err;
    }

    err = register_routes();

    if (err != ESP_OK) {
        httpd_stop(server_);
        server_ = nullptr;
        relay_api_ = {};

        ESP_LOGE(kTag, "Register routes failed");
        return err;
    }

    ESP_LOGI(kTag, "HTTP server started on port %d", CONFIG_WEB_SERVER_PORT);

    return ESP_OK;
}

// ============================================================
// 停止
// ============================================================

esp_err_t WebServer::stop() {
    if (server_ == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = httpd_stop(server_);

    if (err == ESP_OK) {
        server_ = nullptr;
        relay_api_ = {};

        ESP_LOGI(kTag, "HTTP server stopped");
    }

    return err;
}

// ============================================================
// 状态
// ============================================================

bool WebServer::is_running() const { return server_ != nullptr; }

httpd_handle_t WebServer::handle() const { return server_; }

// ============================================================
// 路由注册
// ============================================================

esp_err_t WebServer::register_routes() {
    httpd_uri_t get_uri {};

    get_uri.uri = "/api/relay";
    get_uri.method = HTTP_GET;
    get_uri.handler = get_relays;
    get_uri.user_ctx = this;

    httpd_uri_t put_uri {};

    put_uri.uri = "/api/relay";
    put_uri.method = HTTP_PUT;
    put_uri.handler = put_relay;
    put_uri.user_ctx = this;

    httpd_uri_t off_uri {};

    off_uri.uri = "/api/relay/all-off";
    off_uri.method = HTTP_POST;
    off_uri.handler = all_off;
    off_uri.user_ctx = this;

    esp_err_t err = httpd_register_uri_handler(server_, &get_uri);

    if (err != ESP_OK) return err;

    err = httpd_register_uri_handler(server_, &put_uri);

    if (err != ESP_OK) return err;

    return httpd_register_uri_handler(server_, &off_uri);
}

// ============================================================
// 读取 JSON 请求
// ============================================================

esp_err_t WebServer::read_json(httpd_req_t *req, char **buffer) {
    if (req == nullptr || buffer == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    *buffer = nullptr;

    const size_t length = req->content_len;

    if (length == 0 || length > CONFIG_WEB_SERVER_MAX_REQUEST_SIZE) {
        return ESP_ERR_INVALID_SIZE;
    }

    char *data = static_cast<char *>(std::calloc(length + 1, sizeof(char)));

    if (data == nullptr) {
        return ESP_ERR_NO_MEM;
    }

    size_t received = 0;

    while (received < length) {

        int ret = httpd_req_recv(req, data + received, length - received);

        if (ret <= 0) {
            std::free(data);
            return ESP_FAIL;
        }

        received += static_cast<size_t>(ret);
    }

    data[length] = '\0';

    *buffer = data;

    return ESP_OK;
}

// ============================================================
// JSON 响应
// ============================================================

esp_err_t WebServer::send_json(httpd_req_t *req, const char *json) {
    httpd_resp_set_type(req, "application/json");

    httpd_resp_set_hdr(req, "Cache-Control", "no-store");

    return httpd_resp_sendstr(req, json);
}

// ============================================================
// 错误响应
// ============================================================

esp_err_t WebServer::send_error(
    httpd_req_t *req, int status, const char *message) {
    const char *status_text = "500 Internal Server Error";

    switch (status) {
    case 400: status_text = "400 Bad Request"; break;

    case 413: status_text = "413 Payload Too Large"; break;

    case 500: status_text = "500 Internal Server Error"; break;

    default: break;
    }

    httpd_resp_set_status(req, status_text);

    cJSON *root = cJSON_CreateObject();

    if (root == nullptr) {
        return httpd_resp_sendstr(
            req, R"({"code":500,"message":"Internal server error"})");
    }

    cJSON_AddNumberToObject(root, "code", status);

    cJSON_AddStringToObject(root, "message", message);

    char *json = cJSON_PrintUnformatted(root);

    cJSON_Delete(root);

    if (json == nullptr) {
        return httpd_resp_sendstr(
            req, R"({"code":500,"message":"Internal server error"})");
    }

    esp_err_t err = send_json(req, json);

    std::free(json);

    return err;
}

// ============================================================
// GET /api/relay
// ============================================================

esp_err_t WebServer::get_relays(httpd_req_t *req) {
    auto *self = static_cast<WebServer *>(req->user_ctx);

    if (self == nullptr) {
        return ESP_FAIL;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON *array = cJSON_CreateArray();

    if (root == nullptr || array == nullptr) {
        cJSON_Delete(root);
        cJSON_Delete(array);
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddNumberToObject(root, "code", 0);

    cJSON_AddStringToObject(root, "message", "success");

    cJSON_AddNumberToObject(root, "count", self->relay_api_.count);

    for (uint8_t i = 0; i < self->relay_api_.count; ++i) {

        bool state = false;

        esp_err_t err =
            self->relay_api_.get_state(self->relay_api_.context, i, &state);

        if (err != ESP_OK) {
            cJSON_Delete(root);
            cJSON_Delete(array);

            return send_error(req, 500, "Failed to read relay state");
        }

        cJSON *item = cJSON_CreateObject();

        if (item == nullptr) {
            cJSON_Delete(root);
            cJSON_Delete(array);
            return ESP_ERR_NO_MEM;
        }

        cJSON_AddNumberToObject(item, "id", i);

        cJSON_AddBoolToObject(item, "state", state);

        cJSON_AddItemToArray(array, item);
    }

    cJSON_AddItemToObject(root, "data", array);

    char *json = cJSON_PrintUnformatted(root);

    cJSON_Delete(root);

    if (json == nullptr) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = send_json(req, json);

    std::free(json);

    return err;
}

// ============================================================
// PUT /api/relay
// ============================================================

esp_err_t WebServer::put_relay(httpd_req_t *req) {
    auto *self = static_cast<WebServer *>(req->user_ctx);

    if (self == nullptr) {
        return ESP_FAIL;
    }

    char *buffer = nullptr;

    esp_err_t err = read_json(req, &buffer);

    if (err != ESP_OK) {
        return send_error(req, err == ESP_ERR_INVALID_SIZE ? 413 : 400,
            "Invalid request body");
    }

    cJSON *root = cJSON_Parse(buffer);

    std::free(buffer);

    if (root == nullptr) {
        return send_error(req, 400, "Invalid JSON");
    }

    const cJSON *id_item = cJSON_GetObjectItemCaseSensitive(root, "id");

    const cJSON *state_item = cJSON_GetObjectItemCaseSensitive(root, "state");

    if (!cJSON_IsNumber(id_item) || !cJSON_IsBool(state_item) ||
        id_item->valuedouble < 0 ||
        id_item->valuedouble >= self->relay_api_.count ||
        id_item->valuedouble != static_cast<int>(id_item->valuedouble)) {

        cJSON_Delete(root);

        return send_error(req, 400, "Invalid relay id or state");
    }

    const uint8_t id = static_cast<uint8_t>(id_item->valueint);

    const bool state = cJSON_IsTrue(state_item);

    cJSON_Delete(root);

    err = self->relay_api_.set_state(self->relay_api_.context, id, state);

    if (err != ESP_OK) {
        return send_error(req, 500, "Failed to set relay state");
    }

    char response[128];

    std::snprintf(response, sizeof(response),
        R"({"code":0,"message":"success","data":{"id":%u,"state":%s}})",
        static_cast<unsigned>(id), state ? "true" : "false");

    return send_json(req, response);
}

// ============================================================
// POST /api/relay/all-off
// ============================================================

esp_err_t WebServer::all_off(httpd_req_t *req) {
    auto *self = static_cast<WebServer *>(req->user_ctx);

    if (self == nullptr) {
        return ESP_FAIL;
    }

    for (uint8_t i = 0; i < self->relay_api_.count; ++i) {

        esp_err_t err =
            self->relay_api_.set_state(self->relay_api_.context, i, false);

        if (err != ESP_OK) {
            return send_error(req, 500, "Failed to turn off all relays");
        }
    }

    return send_json(req, R"({"code":0,"message":"success"})");
}
