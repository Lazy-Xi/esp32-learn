#include "http_response.hpp"

#include <utility>

namespace {

const char *status_text(int status) {
    switch (status) {
    case 200: return "200 OK";
    case 201: return "201 Created";
    case 202: return "202 Accepted";
    case 204: return "204 No Content";

    case 301: return "301 Moved Permanently";
    case 302: return "302 Found";
    case 304: return "304 Not Modified";

    case 400: return "400 Bad Request";
    case 401: return "401 Unauthorized";
    case 403: return "403 Forbidden";
    case 404: return "404 Not Found";
    case 405: return "405 Method Not Allowed";
    case 408: return "408 Request Timeout";
    case 409: return "409 Conflict";
    case 413: return "413 Payload Too Large";
    case 415: return "415 Unsupported Media Type";
    case 422: return "422 Unprocessable Entity";
    case 429: return "429 Too Many Requests";

    case 500: return "500 Internal Server Error";
    case 501: return "501 Not Implemented";
    case 502: return "502 Bad Gateway";
    case 503: return "503 Service Unavailable";
    case 504: return "504 Gateway Timeout";

    default: return "500 Internal Server Error";
    }
}

} // namespace

HttpResponse::HttpResponse(
    int status, std::string content_type, std::string body)
    : m_status(status), m_content_type(std::move(content_type)),
      m_body(std::move(body)) {}

HttpResponse HttpResponse::json(const char *json, int status) {
    return HttpResponse(
        status, "application/json", json != nullptr ? json : "null");
}

HttpResponse HttpResponse::json(const std::string &json, int status) {
    return HttpResponse(status, "application/json", json);
}

HttpResponse HttpResponse::json(const cJSON *json, int status) {
    if (json == nullptr) {
        return error(500, "Invalid JSON object");
    }

    char *buffer = cJSON_PrintUnformatted(json);

    if (buffer == nullptr) {
        return error(500, "JSON serialization failed");
    }

    HttpResponse response(status, "application/json", buffer);

    cJSON_free(buffer);

    return response;
}

HttpResponse HttpResponse::text(
    const std::string &text, int status, const char *content_type) {
    return HttpResponse(status,
        content_type != nullptr ? content_type : "text/plain; charset=utf-8",
        text);
}

HttpResponse HttpResponse::error(int status, const char *message) {
    cJSON *root = cJSON_CreateObject();

    if (root == nullptr) {
        return HttpResponse(500, "application/json",
            R"({"code":500,"error":"Internal server error"})");
    }

    const bool success =
        cJSON_AddNumberToObject(root, "code", status) != nullptr &&
        cJSON_AddStringToObject(root, "error",
            message != nullptr ? message : "Unknown error") != nullptr;

    if (!success) {
        cJSON_Delete(root);

        return HttpResponse(500, "application/json",
            R"({"code":500,"error":"Internal server error"})");
    }

    HttpResponse response = HttpResponse::json(root, status);

    cJSON_Delete(root);

    return response;
}

HttpResponse &HttpResponse::header(const char *name, const char *value) {
    if (name != nullptr && value != nullptr) {
        m_headers.emplace_back(name, value);
    }

    return *this;
}

int HttpResponse::status() const noexcept { return m_status; }

esp_err_t HttpResponse::send(httpd_req_t *req) const {
    if (req == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = httpd_resp_set_status(req, status_text(m_status));

    if (err != ESP_OK) {
        return err;
    }

    if (!m_content_type.empty()) {
        err = httpd_resp_set_type(req, m_content_type.c_str());

        if (err != ESP_OK) {
            return err;
        }
    }

    err = httpd_resp_set_hdr(req, "Cache-Control", "no-store");

    if (err != ESP_OK) {
        return err;
    }

    for (const auto &item : m_headers) {
        err = httpd_resp_set_hdr(req, item.first.c_str(), item.second.c_str());

        if (err != ESP_OK) {
            return err;
        }
    }

    if (m_stream_handler) {
        return m_stream_handler(req);
    }

    if (m_status == 204 || m_status == 304) {
        return httpd_resp_send(req, nullptr, 0);
    }

    return httpd_resp_send(req, m_body.empty() ? nullptr : m_body.c_str(),
        static_cast<ssize_t>(m_body.size()));
}

HttpResponse HttpResponse::stream(
    std::string content_type, StreamHandler handler) {
    HttpResponse response(200, std::move(content_type), {});

    response.m_stream_handler = std::move(handler);

    return response;
}
