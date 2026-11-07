#pragma once

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "cJSON.h"
#include "esp_err.h"
#include "esp_http_server.h"

class WebServer;

class HttpResponse {
public:
    using StreamHandler = std::function<esp_err_t(httpd_req_t *req)>;

    HttpResponse() = default;

    static HttpResponse json(const char *json, int status = 200);
    static HttpResponse json(const std::string &json, int status = 200);
    static HttpResponse json(const cJSON *json, int status = 200);

    static HttpResponse text(const std::string &text, int status = 200,
        const char *content_type = "text/plain; charset=utf-8");
    static HttpResponse error(int status, const char *message);

    // 创建流式响应
    static HttpResponse stream(std::string content_type, StreamHandler handler);

    HttpResponse &header(const char *name, const char *value);
    int status() const noexcept;

private:
    friend class WebServer;

    HttpResponse(int status, std::string content_type, std::string body);

    esp_err_t send(httpd_req_t *req) const;

private:
    int m_status = 200;

    std::string m_content_type = "application/json";

    std::string m_body = "null";

    std::vector<std::pair<std::string, std::string>> m_headers;

    StreamHandler m_stream_handler;
};
