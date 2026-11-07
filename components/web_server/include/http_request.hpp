#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "cJSON.h"
#include "esp_err.h"
#include "esp_http_server.h"

#undef HTTP_ANY

const httpd_method_t HTTP_ANY = static_cast<httpd_method_t>(INT_MAX);

class WebServer;

class HttpRequest {
public:
    std::string param(const char *name) const;
    bool has_param(const char *name) const;

    std::string query(const char *name) const;

    std::string uri() const;
    httpd_method_t method() const noexcept;
    size_t content_length() const noexcept;

    // 获取原生请求句柄
    httpd_req_t *native() const noexcept;

    esp_err_t read_json(cJSON **root) const;

private:
    friend class WebServer;

    HttpRequest(httpd_req_t *req,
        std::vector<std::pair<std::string, std::string>> params,
        size_t max_request_size);

private:
    httpd_req_t *m_req = nullptr;

    std::vector<std::pair<std::string, std::string>> m_params;

    size_t m_max_request_size = 2048;
};
