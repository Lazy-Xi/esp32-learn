
#include "relay_http_server.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string_view>
#include <strings.h>

#include "cJSON.h"
#include "esp_log.h"

// ============================================================
// Configuration
// ============================================================

namespace {

constexpr char kTag[] = "relay_http";

constexpr char kFsRoot[] = "/littlefs";

constexpr std::size_t kMaxBody = 128;
constexpr std::size_t kMaxUriPath = 256;
constexpr std::size_t kFileBufferSize = 1024;

// ============================================================
// JSON Response
// ============================================================

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

    if (close) {
        httpd_resp_set_hdr(request, "Connection", "close");
    }

    const auto result = json(request, status, body);

    // 有未消费的请求体时关闭连接，
    // 避免残留字节成为下一条请求。
    return close ? ESP_FAIL : result;
}

// ============================================================
// Content-Type Validation
// ============================================================

bool is_json(httpd_req_t *request) {
    char content_type[64] {};

    if (httpd_req_get_hdr_value_str(request, "Content-Type", content_type,
            sizeof(content_type)) != ESP_OK) {
        return false;
    }

    constexpr char expected[] = "application/json";
    constexpr auto length = sizeof(expected) - 1;

    return strncasecmp(content_type, expected, length) == 0 &&
           (content_type[length] == '\0' || content_type[length] == ';');
}

// ============================================================
// MIME Type
// ============================================================

const char *mime_type(const char *path) {
    const char *ext = std::strrchr(path, '.');

    if (ext == nullptr) {
        return "application/octet-stream";
    }

    if (strcasecmp(ext, ".html") == 0) {
        return "text/html; charset=utf-8";
    }

    if (strcasecmp(ext, ".htm") == 0) {
        return "text/html; charset=utf-8";
    }

    if (strcasecmp(ext, ".js") == 0 || strcasecmp(ext, ".mjs") == 0) {
        return "application/javascript";
    }

    if (strcasecmp(ext, ".css") == 0) {
        return "text/css; charset=utf-8";
    }

    if (strcasecmp(ext, ".json") == 0) {
        return "application/json";
    }

    if (strcasecmp(ext, ".map") == 0) {
        return "application/json";
    }

    if (strcasecmp(ext, ".svg") == 0) {
        return "image/svg+xml";
    }

    if (strcasecmp(ext, ".png") == 0) {
        return "image/png";
    }

    if (strcasecmp(ext, ".jpg") == 0 || strcasecmp(ext, ".jpeg") == 0) {
        return "image/jpeg";
    }

    if (strcasecmp(ext, ".gif") == 0) {
        return "image/gif";
    }

    if (strcasecmp(ext, ".ico") == 0) {
        return "image/x-icon";
    }

    if (strcasecmp(ext, ".webp") == 0) {
        return "image/webp";
    }

    if (strcasecmp(ext, ".avif") == 0) {
        return "image/avif";
    }

    if (strcasecmp(ext, ".woff") == 0) {
        return "font/woff";
    }

    if (strcasecmp(ext, ".woff2") == 0) {
        return "font/woff2";
    }

    if (strcasecmp(ext, ".ttf") == 0) {
        return "font/ttf";
    }

    if (strcasecmp(ext, ".otf") == 0) {
        return "font/otf";
    }

    if (strcasecmp(ext, ".wasm") == 0) {
        return "application/wasm";
    }

    if (strcasecmp(ext, ".txt") == 0) {
        return "text/plain; charset=utf-8";
    }

    if (strcasecmp(ext, ".xml") == 0) {
        return "application/xml";
    }

    if (strcasecmp(ext, ".pdf") == 0) {
        return "application/pdf";
    }

    if (strcasecmp(ext, ".mp3") == 0) {
        return "audio/mpeg";
    }

    if (strcasecmp(ext, ".mp4") == 0) {
        return "video/mp4";
    }

    return "application/octet-stream";
}

// ============================================================
// URI Validation
// ============================================================

bool valid_uri_path(const char *path) {
    if (path == nullptr || path[0] != '/') {
        return false;
    }

    // 不允许反斜杠
    if (std::strchr(path, '\\') != nullptr) {
        return false;
    }

    // 不允许控制字符
    for (const unsigned char *p = reinterpret_cast<const unsigned char *>(path);
        *p != '\0'; ++p) {
        if (*p < 0x20 || *p == 0x7f) {
            return false;
        }
    }

    // 检查路径段，防止目录穿越
    const char *segment = path;

    while (*segment != '\0') {
        while (*segment == '/') {
            ++segment;
        }

        const char *end = segment;

        while (*end != '\0' && *end != '/') {
            ++end;
        }

        const auto length = end - segment;

        if ((length == 2 && segment[0] == '.' && segment[1] == '.') ||
            (length == 1 && segment[0] == '.')) {
            return false;
        }

        segment = end;
    }

    return true;
}

// ============================================================
// LittleFS File Response
// ============================================================

esp_err_t send_file(httpd_req_t *request, const char *path,
    const char *content_type, const char *cache_control) {
    FILE *file = std::fopen(path, "rb");

    if (file == nullptr) {
        ESP_LOGW(kTag, "File not found: %s", path);

        return httpd_resp_send_err(
            request, HTTPD_404_NOT_FOUND, "File not found");
    }

    httpd_resp_set_type(request, content_type);

    httpd_resp_set_hdr(request, "Cache-Control", cache_control);

    char buffer[kFileBufferSize];

    while (true) {
        const auto count = std::fread(buffer, 1, sizeof(buffer), file);

        if (count > 0) {
            const auto result = httpd_resp_send_chunk(request, buffer, count);

            if (result != ESP_OK) {
                ESP_LOGW(kTag, "HTTP file transmission failed: %s", path);

                std::fclose(file);
                return result;
            }
        }

        if (count < sizeof(buffer)) {
            if (std::ferror(file)) {
                ESP_LOGE(kTag, "File read error: %s", path);

                std::fclose(file);
                return ESP_FAIL;
            }

            break;
        }
    }

    std::fclose(file);

    // 结束 HTTP Chunked Response
    return httpd_resp_send_chunk(request, nullptr, 0);
}

} // namespace

// ============================================================
// Static Web Page
// ============================================================

esp_err_t RelayHttpServer::page(httpd_req_t *request) {
    ESP_LOGD(kTag, "Request index.html");

    return send_file(request, "/littlefs/index.html",
        "text/html; charset=utf-8", "no-store");
}

// ============================================================
// Static File Handler
// ============================================================

esp_err_t RelayHttpServer::static_file(httpd_req_t *request) {
    char uri[kMaxUriPath + 1] {};

    // 去除 URL 查询参数
    const char *query = std::strchr(request->uri, '?');

    const std::size_t length =
        query ? static_cast<std::size_t>(query - request->uri)
              : std::strlen(request->uri);

    if (length == 0 || length > kMaxUriPath) {
        return httpd_resp_send_err(
            request, HTTPD_400_BAD_REQUEST, "Invalid URI");
    }

    std::memcpy(uri, request->uri, length);

    uri[length] = '\0';

    // API 路径不作为静态资源处理
    if (std::strncmp(uri, "/api/", 5) == 0 || std::strcmp(uri, "/api") == 0) {
        return httpd_resp_send_err(
            request, HTTPD_404_NOT_FOUND, "API not found");
    }

    // URI 安全检查
    if (!valid_uri_path(uri)) {
        return httpd_resp_send_err(
            request, HTTPD_400_BAD_REQUEST, "Invalid file path");
    }

    // 拼接 LittleFS 文件路径
    char path[sizeof(kFsRoot) + kMaxUriPath + 1];

    const int written = std::snprintf(path, sizeof(path), "%s%s", kFsRoot, uri);

    if (written < 0 || static_cast<std::size_t>(written) >= sizeof(path)) {
        return ESP_FAIL;
    }

    ESP_LOGD(kTag, "Static file request: %s", path);

    return send_file(request, path, mime_type(path), "no-cache");
}

// ============================================================
// Relay State
// ============================================================

esp_err_t RelayHttpServer::send_state(httpd_req_t *request) {
    const auto &self = *static_cast<RelayHttpServer *>(request->user_ctx);

    const auto state = self.relays_.state();

    char body[384];

    const auto boolean = [](bool value) { return value ? "true" : "false"; };

    std::snprintf(body, sizeof(body),

        "{\"online\":%s,\"relays\":["
        "{\"id\":1,\"gpio\":%d,\"on\":%s},"
        "{\"id\":2,\"gpio\":%d,\"on\":%s},"
        "{\"id\":3,\"gpio\":%d,\"on\":%s},"
        "{\"id\":4,\"gpio\":%d,\"on\":%s}]}",

        boolean(state.online),

        static_cast<int>(RelayController::pins[0]), boolean(state.on[0]),

        static_cast<int>(RelayController::pins[1]), boolean(state.on[1]),

        static_cast<int>(RelayController::pins[2]), boolean(state.on[2]),

        static_cast<int>(RelayController::pins[3]), boolean(state.on[3]));

    return json(request, "200 OK", body);
}

// ============================================================
// GET /api/relays
// ============================================================

esp_err_t RelayHttpServer::get_relays(httpd_req_t *request) {
    return send_state(request);
}

// ============================================================
// PUT /api/relays/{id}
// ============================================================

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
        return error(
            request, "413 Content Too Large", "Body exceeds 128 bytes", true);
    }

    if (request->content_len == 0) {
        return error(request, "400 Bad Request", "Expected a JSON body");
    }

    char body[kMaxBody + 1] {};

    std::size_t received = 0;

    while (received < request->content_len) {
        const int count = httpd_req_recv(
            request, body + received, request->content_len - received);

        if (count == HTTPD_SOCK_ERR_TIMEOUT) {
            return error(
                request, "408 Request Timeout", "Request body timed out", true);
        }

        if (count <= 0) {
            return ESP_FAIL;
        }

        received += static_cast<std::size_t>(count);
    }

    // 只接受扁平 JSON 对象
    // 排除嵌套输入，限制解析开销
    if (std::count(body, body + received, '{') != 1 ||

        std::find(body, body + received, '[') != body + received) {
        return error(
            request, "400 Bad Request", "Expected one boolean field named on");
    }

    const char *parse_end = nullptr;

    cJSON *document =
        cJSON_ParseWithLengthOpts(body, received + 1, &parse_end, true);

    const cJSON *value = cJSON_GetObjectItemCaseSensitive(document, "on");

    // 只允许一个布尔字段
    const bool valid = cJSON_IsObject(document) && cJSON_IsBool(value) &&
                       cJSON_GetArraySize(document) == 1 &&
                       parse_end == body + received;

    const bool on = cJSON_IsTrue(value);

    cJSON_Delete(document);

    if (!valid) {
        return error(
            request, "400 Bad Request", "Expected one boolean field named on");
    }

    auto &self = *static_cast<RelayHttpServer *>(request->user_ctx);

    const auto channel = static_cast<std::size_t>(path.back() - '0');

    const auto result = self.relays_.set(channel, on);

    if (result == ESP_ERR_INVALID_STATE) {
        return error(request, "503 Service Unavailable", "WiFi is offline");
    }

    if (result != ESP_OK) {
        return error(
            request, "500 Internal Server Error", "GPIO update failed");
    }

    ESP_LOGI(
        kTag, "Relay %u %s", static_cast<unsigned>(channel), on ? "ON" : "OFF");

    return send_state(request);
}

// ============================================================
// POST /api/relays/all-off
// ============================================================

esp_err_t RelayHttpServer::all_off(httpd_req_t *request) {
    if (!is_json(request)) {
        return error(request, "415 Unsupported Media Type",
            "Content-Type must be application/json", true);
    }

    if (request->content_len != 0) {
        return error(
            request, "400 Bad Request", "This endpoint takes no body", true);
    }

    auto &self = *static_cast<RelayHttpServer *>(request->user_ctx);

    if (self.relays_.all_off() != ESP_OK) {
        return error(
            request, "500 Internal Server Error", "GPIO update failed");
    }

    return send_state(request);
}

// ============================================================
// Start HTTP Server
// ============================================================

esp_err_t RelayHttpServer::start() {
    if (server_ != nullptr) {
        return ESP_OK;
    }

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

    // ========================================================
    // Route Registration
    // ========================================================

    const struct Route {
        const char *uri;
        httpd_method_t method;
        esp_err_t (*handler)(httpd_req_t *);
    } routes[] = {

        // Relay API
        {"/api/relays", HTTP_GET, get_relays},

        {"/api/relays/all-off", HTTP_POST, all_off},

        {"/api/relays/*", HTTP_PUT, put_relay},

        // Web homepage
        {"/", HTTP_GET, page},

        // Static resources
        {"/*", HTTP_GET, static_file}};

    for (const auto &route : routes) {
        httpd_uri_t uri {};

        uri.uri = route.uri;
        uri.method = route.method;
        uri.handler = route.handler;
        uri.user_ctx = this;

        const auto err = httpd_register_uri_handler(server_, &uri);

        if (err != ESP_OK) {
            ESP_LOGE(kTag, "Register route failed: %s", route.uri);

            const auto stop_result = stop();

            if (stop_result != ESP_OK) {
                ESP_LOGE(
                    kTag, "Cleanup failed: %s", esp_err_to_name(stop_result));
            }

            return err;
        }

        ESP_LOGD(kTag, "Registered route: %s", route.uri);
    }

    ESP_LOGI(kTag, "HTTP server listening on port 80");

    ESP_LOGI(kTag, "LittleFS root: %s", kFsRoot);

    return ESP_OK;
}

// ============================================================
// Stop HTTP Server
// ============================================================

esp_err_t RelayHttpServer::stop() {
    if (server_ == nullptr) {
        return ESP_OK;
    }

    const auto result = httpd_stop(server_);

    if (result == ESP_OK) {
        server_ = nullptr;

        ESP_LOGI(kTag, "HTTP server stopped");
    }

    return result;
}
