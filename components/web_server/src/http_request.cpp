#include "http_request.hpp"

#include <cstdlib>
#include <cstring>

HttpRequest::HttpRequest(httpd_req_t *req,
    std::vector<std::pair<std::string, std::string>> params,
    size_t max_request_size)
    : m_req(req), m_params(std::move(params)),
      m_max_request_size(max_request_size) {}

std::string HttpRequest::param(const char *name) const {
    if (name == nullptr) {
        return {};
    }

    for (const auto &item : m_params) {
        if (item.first == name) {
            return item.second;
        }
    }

    return {};
}

bool HttpRequest::has_param(const char *name) const {
    if (name == nullptr) {
        return false;
    }

    for (const auto &item : m_params) {
        if (item.first == name) {
            return true;
        }
    }

    return false;
}

std::string HttpRequest::query(const char *name) const {
    if (m_req == nullptr || name == nullptr) {
        return {};
    }

    const size_t length = httpd_req_get_url_query_len(m_req);

    if (length == 0) {
        return {};
    }

    std::string query_string(length + 1, '\0');

    esp_err_t err = httpd_req_get_url_query_str(
        m_req, &query_string[0], query_string.size());

    if (err != ESP_OK) {
        return {};
    }

    std::string value(length + 1, '\0');

    err = httpd_query_key_value(
        query_string.c_str(), name, &value[0], value.size());

    if (err != ESP_OK) {
        return {};
    }

    value.resize(std::strlen(value.c_str()));

    return value;
}

std::string HttpRequest::uri() const {
    if (m_req == nullptr) {
        return {};
    }

    return m_req->uri;
}

httpd_method_t HttpRequest::method() const noexcept {
    if (m_req == nullptr) {
        return static_cast<httpd_method_t>(HTTP_ANY);
    }

    return static_cast<httpd_method_t>(m_req->method);
}

size_t HttpRequest::content_length() const noexcept {
    return m_req != nullptr ? m_req->content_len : 0;
}

httpd_req_t *HttpRequest::native() const noexcept { return m_req; }

esp_err_t HttpRequest::read_json(cJSON **root) const {
    if (m_req == nullptr || root == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    *root = nullptr;

    const size_t length = m_req->content_len;

    if (length == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (length > m_max_request_size) {
        return ESP_ERR_INVALID_SIZE;
    }

    char *buffer = static_cast<char *>(std::calloc(length + 1, sizeof(char)));

    if (buffer == nullptr) {
        return ESP_ERR_NO_MEM;
    }

    size_t received = 0;

    while (received < length) {
        const int ret =
            httpd_req_recv(m_req, buffer + received, length - received);

        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            std::free(buffer);
            return ESP_ERR_TIMEOUT;
        }

        if (ret <= 0) {
            std::free(buffer);
            return ESP_FAIL;
        }

        received += static_cast<size_t>(ret);
    }

    buffer[length] = '\0';

    cJSON *parsed = cJSON_ParseWithOpts(buffer, nullptr, true);

    std::free(buffer);

    if (parsed == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    *root = parsed;

    return ESP_OK;
}
