#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "esp_err.h"
#include "esp_http_server.h"

#include "http_request.hpp"
#include "http_response.hpp"

struct WebServerConfig {
    uint16_t port = 80;
    uint16_t max_open_sockets = 4;
    uint16_t max_uri_handlers = 1;
    uint32_t stack_size = 6144;
    size_t max_request_size = 2048;

    bool lru_purge_enable = true;
};

struct StaticFilesConfig {
    std::string index_file;
    std::string cache_control = "public, max-age=3600";
    bool spa_fallback = false;

    StaticFilesConfig() : index_file("index.html") {}
};

class WebServer {
public:
    // Lambda Handler
    using Handler = std::function<HttpResponse(const HttpRequest &req)>;

    explicit WebServer(const WebServerConfig &config = {});

    ~WebServer();

    WebServer(const WebServer &) = delete;
    WebServer &operator=(const WebServer &) = delete;

    esp_err_t route(const char *uri, httpd_method_t method, Handler handler);

    esp_err_t get(const char *uri, Handler handler);
    esp_err_t post(const char *uri, Handler handler);
    esp_err_t put(const char *uri, Handler handler);
    esp_err_t del(const char *uri, Handler handler);

    esp_err_t start();
    esp_err_t stop();

    bool is_running() const noexcept;
    size_t route_count() const noexcept;
    httpd_handle_t handle() const noexcept;

    esp_err_t static_files(const char *url_prefix, const char *filesystem_root,
        const StaticFilesConfig &options = {});

private:
    struct PathPart {
        std::string value;
        bool parameter = false;
        bool tail = false;
    };

    struct RouteEntry {
        std::string pattern;
        httpd_method_t method = HTTP_GET;
        Handler handler;
        std::vector<PathPart> parts;
        size_t static_count = 0;
    };

    static bool parse_pattern(
        const char *uri, std::vector<PathPart> &parts, size_t &static_count);

    static bool split_request_path(
        const char *uri, std::vector<std::string> &parts);

    static bool decode_segment(const std::string &input, std::string &output);

    static bool match_route(const RouteEntry &route,
        const std::vector<std::string> &path,
        std::vector<std::pair<std::string, std::string>> &params);

    static esp_err_t dispatch(httpd_req_t *req);
    esp_err_t dispatch_request(httpd_req_t *req);

private:
    WebServerConfig m_config;

    httpd_handle_t m_server = nullptr;

    // 全部业务路由
    std::vector<RouteEntry> m_routes;

    // 底层统一入口
    httpd_uri_t m_catch_all_uri {};
};
