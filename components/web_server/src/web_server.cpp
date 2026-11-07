#include "web_server.hpp"

#include <utility>

#include "esp_log.h"

namespace {

constexpr char kTag[] = "WebServer";

bool valid_parameter_name(const std::string &name) {
    if (name.empty()) {
        return false;
    }

    auto is_alpha = [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
    };

    auto is_digit = [](char c) { return c >= '0' && c <= '9'; };

    if (!is_alpha(name[0])) {
        return false;
    }

    for (char c : name) {
        if (!is_alpha(c) && !is_digit(c)) {
            return false;
        }
    }

    return true;
}

int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;

    return -1;
}

const char *method_name(httpd_method_t method) {
    switch (method) {
    case HTTP_GET: return "GET";
    case HTTP_POST: return "POST";
    case HTTP_PUT: return "PUT";
    case HTTP_DELETE: return "DELETE";
    case HTTP_HEAD: return "HEAD";
    case HTTP_OPTIONS: return "OPTIONS";
    case HTTP_PATCH: return "PATCH";

    default: return "UNKNOWN";
    }
}

} // namespace

WebServer::WebServer(const WebServerConfig &config) : m_config(config) {}

WebServer::~WebServer() {
    if (m_server != nullptr) {
        esp_err_t err = stop();

        if (err != ESP_OK) {
            ESP_LOGE(kTag, "Failed to stop server: %s", esp_err_to_name(err));
        }
    }
}

// 解析路由模板
// 示例：
// /api/relays/<name>/toggle
//
// 结果：
//   api
//   relays
//   <name> (parameter)
//   toggle

bool WebServer::parse_pattern(
    const char *uri, std::vector<PathPart> &parts, size_t &static_count) {
    parts.clear();
    static_count = 0;

    if (uri == nullptr || uri[0] != '/') {
        return false;
    }

    const std::string path(uri);

    if (path.find('?') != std::string::npos ||
        path.find('#') != std::string::npos) {
        return false;
    }

    if (path == "/") {
        return true;
    }

    if (path.back() == '/') {
        return false;
    }

    size_t pos = 1;

    while (pos < path.size()) {
        const size_t end = path.find('/', pos);

        const std::string segment = path.substr(
            pos, end == std::string::npos ? std::string::npos : end - pos);

        if (segment.empty()) {
            return false;
        }

        PathPart part {};

        if (segment.front() == '<' || segment.back() == '>') {

            if (segment.size() < 3 || segment.front() != '<' ||
                segment.back() != '>') {
                return false;
            }

            std::string name = segment.substr(1, segment.size() - 2);

            // <path...> 尾部通配参数
            if (name.size() > 3 &&
                name.compare(name.size() - 3, 3, "...") == 0) {

                name.resize(name.size() - 3);

                part.tail = true;

                // 尾部通配参数必须是最后一个路径段
                if (end != std::string::npos) {
                    return false;
                }
            }

            if (!valid_parameter_name(name)) {
                return false;
            }

            for (const auto &existing : parts) {
                if (existing.parameter && existing.value == name) {
                    return false;
                }
            }

            part.value = std::move(name);
            part.parameter = true;
        }
        else {
            if (segment.find('<') != std::string::npos ||
                segment.find('>') != std::string::npos) {
                return false;
            }

            part.value = segment;
            ++static_count;
        }

        parts.push_back(std::move(part));

        if (end == std::string::npos) {
            break;
        }

        pos = end + 1;
    }

    return true;
}

bool WebServer::split_request_path(
    const char *uri, std::vector<std::string> &parts) {
    parts.clear();

    if (uri == nullptr || uri[0] != '/') {
        return false;
    }

    std::string path(uri);

    const size_t query_pos = path.find('?');

    if (query_pos != std::string::npos) {
        path.resize(query_pos);
    }

    if (path == "/") {
        return true;
    }

    // 允许目录 URL 带尾部斜杠
    while (path.size() > 1 && path.back() == '/') {
        path.pop_back();
    }

    size_t pos = 1;

    while (pos < path.size()) {
        const size_t end = path.find('/', pos);

        std::string segment = path.substr(
            pos, end == std::string::npos ? std::string::npos : end - pos);

        if (segment.empty()) {
            return false;
        }

        parts.push_back(std::move(segment));

        if (end == std::string::npos) {
            break;
        }

        pos = end + 1;
    }

    return true;
}

bool WebServer::decode_segment(const std::string &input, std::string &output) {
    output.clear();

    for (size_t i = 0; i < input.size(); ++i) {
        char c = input[i];

        if (c == '%') {
            if (i + 2 >= input.size()) {
                return false;
            }

            const int high = hex_value(input[i + 1]);
            const int low = hex_value(input[i + 2]);

            if (high < 0 || low < 0) {
                return false;
            }

            c = static_cast<char>((high << 4) | low);

            i += 2;
        }

        if (c == '/' || c == '\\' || c == '\0') {
            return false;
        }

        output.push_back(c);
    }

    return true;
}

bool WebServer::match_route(const RouteEntry &route,
    const std::vector<std::string> &path,
    std::vector<std::pair<std::string, std::string>> &params) {
    params.clear();

    const bool has_tail = !route.parts.empty() && route.parts.back().tail;

    const size_t fixed_count =
        has_tail ? route.parts.size() - 1 : route.parts.size();

    if (has_tail) {
        if (path.size() < fixed_count) {
            return false;
        }
    }
    else if (path.size() != fixed_count) {
        return false;
    }

    // 匹配固定路径段
    for (size_t i = 0; i < fixed_count; ++i) {
        const PathPart &expected = route.parts[i];

        std::string actual;

        if (!decode_segment(path[i], actual)) {
            return false;
        }

        if (expected.parameter) {
            params.emplace_back(expected.value, std::move(actual));
        }
        else if (expected.value != actual) {
            return false;
        }
    }

    // 匹配剩余路径
    if (has_tail) {
        std::string tail;

        for (size_t i = fixed_count; i < path.size(); ++i) {
            std::string segment;

            if (!decode_segment(path[i], segment)) {
                return false;
            }

            if (segment == "." || segment == "..") {
                return false;
            }

            if (!tail.empty()) {
                tail += '/';
            }

            tail += segment;
        }

        params.emplace_back(route.parts.back().value, std::move(tail));
    }

    return true;
}

esp_err_t WebServer::route(
    const char *uri, httpd_method_t method, Handler handler) {
    if (m_server != nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    if (uri == nullptr || !handler) {
        return ESP_ERR_INVALID_ARG;
    }

    std::vector<PathPart> parts;

    size_t static_count = 0;

    if (!parse_pattern(uri, parts, static_count)) {
        return ESP_ERR_INVALID_ARG;
    }

    // 检查重复路由
    for (const auto &existing : m_routes) {
        if (existing.pattern == uri && existing.method == method) {
            return ESP_ERR_INVALID_STATE;
        }
    }

    RouteEntry entry {};

    entry.pattern = uri;
    entry.method = method;
    entry.handler = std::move(handler);
    entry.parts = std::move(parts);
    entry.static_count = static_count;

    m_routes.push_back(std::move(entry));

    ESP_LOGD(kTag, "Route registered: %s", uri);

    return ESP_OK;
}

esp_err_t WebServer::get(const char *uri, Handler handler) {
    return route(uri, HTTP_GET, std::move(handler));
}

esp_err_t WebServer::post(const char *uri, Handler handler) {
    return route(uri, HTTP_POST, std::move(handler));
}

esp_err_t WebServer::put(const char *uri, Handler handler) {
    return route(uri, HTTP_PUT, std::move(handler));
}

esp_err_t WebServer::del(const char *uri, Handler handler) {
    return route(uri, HTTP_DELETE, std::move(handler));
}

esp_err_t WebServer::dispatch(httpd_req_t *req) {
    if (req == nullptr || req->user_ctx == nullptr) {
        return ESP_FAIL;
    }

    auto *self = static_cast<WebServer *>(req->user_ctx);

    return self->dispatch_request(req);
}

esp_err_t WebServer::dispatch_request(httpd_req_t *req) {
    std::vector<std::string> path;

    if (!split_request_path(req->uri, path)) {
        return HttpResponse::error(400, "Invalid request path").send(req);
    }

    RouteEntry *selected = nullptr;

    std::vector<std::pair<std::string, std::string>> selected_params;

    bool path_found = false;

    size_t best_score = 0;

    std::string allowed_methods;

    const auto append_method = [&allowed_methods](const char *name) {
        if (!allowed_methods.empty()) {
            allowed_methods += ", ";
        }

        allowed_methods += name;
    };

    const auto request_method = static_cast<httpd_method_t>(req->method);

    for (auto &route : m_routes) {

        std::vector<std::pair<std::string, std::string>> params;

        if (!match_route(route, path, params)) {
            continue;
        }

        path_found = true;

        // 检查 HTTP Method
        const bool method_matches =
            route.method == HTTP_ANY || route.method == request_method;

        if (!method_matches) {
            if (route.method != HTTP_ANY) {
                append_method(method_name(route.method));
            }

            continue;
        }

        // 优先选择静态路径更多的路由
        // 同等路径优先选择具体 Method 而非 HTTP_ANY
        const bool has_tail = !route.parts.empty() && route.parts.back().tail;
        const size_t score = route.static_count * 1000 + (has_tail ? 0 : 100) +
                             route.parts.size();

        if (selected == nullptr || score > best_score) {
            selected = &route;

            selected_params = std::move(params);

            best_score = score;
        }
    }

    if (selected == nullptr) {

        if (path_found) {
            HttpResponse response =
                HttpResponse::error(405, "Method not allowed");

            if (!allowed_methods.empty()) {
                response.header("Allow", allowed_methods.c_str());
            }

            return response.send(req);
        }

        return HttpResponse::error(404, "Route not found").send(req);
    }

    HttpRequest request(
        req, std::move(selected_params), m_config.max_request_size);
    HttpResponse response = selected->handler(request);

    return response.send(req);
}

esp_err_t WebServer::start() {
    if (m_server != nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    config.server_port = m_config.port;

    config.max_open_sockets = m_config.max_open_sockets;

    config.max_uri_handlers =
        m_config.max_uri_handlers > 0 ? m_config.max_uri_handlers : 1;

    config.stack_size = m_config.stack_size > 0 ? m_config.stack_size : 6144;

    config.lru_purge_enable = m_config.lru_purge_enable;

    // 使用 ESP-IDF 通配匹配器
    config.uri_match_fn = httpd_uri_match_wildcard;

    esp_err_t err = httpd_start(&m_server, &config);

    if (err != ESP_OK) {
        m_server = nullptr;

        ESP_LOGE(kTag, "HTTP server start failed: %s", esp_err_to_name(err));

        return err;
    }

    m_catch_all_uri = {};
    m_catch_all_uri.uri = "*";
    m_catch_all_uri.method = static_cast<httpd_method_t>(HTTP_ANY);
    m_catch_all_uri.handler = dispatch;
    m_catch_all_uri.user_ctx = this;

    err = httpd_register_uri_handler(m_server, &m_catch_all_uri);

    if (err != ESP_OK) {
        ESP_LOGE(kTag, "Register dispatcher failed: %s", esp_err_to_name(err));

        const esp_err_t stop_err = httpd_stop(m_server);

        if (stop_err == ESP_OK) {
            m_server = nullptr;
            m_catch_all_uri = {};
        }
        else {
            ESP_LOGE(kTag, "Cleanup failed: %s", esp_err_to_name(stop_err));
        }

        return err;
    }

    ESP_LOGI(kTag, "HTTP server started: port=%u, routes=%u",
        static_cast<unsigned>(config.server_port),
        static_cast<unsigned>(m_routes.size()));

    return ESP_OK;
}

esp_err_t WebServer::stop() {
    if (m_server == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = httpd_stop(m_server);

    if (err != ESP_OK) {
        ESP_LOGE(kTag, "HTTP server stop failed: %s", esp_err_to_name(err));

        return err;
    }

    m_server = nullptr;

    m_catch_all_uri = {};

    ESP_LOGI(kTag, "HTTP server stopped");

    return ESP_OK;
}

bool WebServer::is_running() const noexcept { return m_server != nullptr; }

size_t WebServer::route_count() const noexcept { return m_routes.size(); }

httpd_handle_t WebServer::handle() const noexcept { return m_server; }
