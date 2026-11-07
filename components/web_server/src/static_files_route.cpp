#include "web_server.hpp"

#include "esp_log.h"

#include <cctype>
#include <cstdio>
#include <memory>
#include <string>

#include <sys/stat.h>

namespace {

bool is_safe_relative_path(const std::string &path) {
    if (path.empty() || path.front() == '/' ||
        path.find('\\') != std::string::npos ||
        path.find('\0') != std::string::npos) {
        return false;
    }

    size_t pos = 0;

    while (pos < path.size()) {
        const size_t end = path.find('/', pos);

        const std::string segment = path.substr(
            pos, end == std::string::npos ? std::string::npos : end - pos);

        if (segment.empty() || segment == "." || segment == "..") {
            return false;
        }

        if (end == std::string::npos) {
            break;
        }

        pos = end + 1;
    }

    return true;
}

std::string join_path(const std::string &root, const std::string &relative) {
    if (root == "/") {
        return root + relative;
    }

    return root + "/" + relative;
}

bool is_extensionless(const std::string &path) {
    const size_t slash = path.find_last_of('/');

    const std::string filename =
        path.substr(slash == std::string::npos ? 0 : slash + 1);

    return filename.find('.') == std::string::npos;
}

std::string get_mime_type(const std::string &path) {
    const size_t dot = path.find_last_of('.');

    if (dot == std::string::npos) {
        return "application/octet-stream";
    }

    std::string ext = path.substr(dot + 1);

    for (char &c : ext) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    if (ext == "html" || ext == "htm") {
        return "text/html; charset=utf-8";
    }

    if (ext == "css") {
        return "text/css; charset=utf-8";
    }

    if (ext == "js" || ext == "mjs") {
        return "application/javascript";
    }

    if (ext == "json" || ext == "map") {
        return "application/json";
    }

    if (ext == "svg") {
        return "image/svg+xml";
    }

    if (ext == "png") {
        return "image/png";
    }

    if (ext == "jpg" || ext == "jpeg") {
        return "image/jpeg";
    }

    if (ext == "gif") {
        return "image/gif";
    }

    if (ext == "ico") {
        return "image/x-icon";
    }

    if (ext == "webp") {
        return "image/webp";
    }

    if (ext == "avif") {
        return "image/avif";
    }

    if (ext == "woff") {
        return "font/woff";
    }

    if (ext == "woff2") {
        return "font/woff2";
    }

    if (ext == "ttf") {
        return "font/ttf";
    }

    if (ext == "otf") {
        return "font/otf";
    }

    if (ext == "eot") {
        return "application/vnd.ms-fontobject";
    }

    if (ext == "txt") {
        return "text/plain; charset=utf-8";
    }

    if (ext == "xml") {
        return "application/xml";
    }

    if (ext == "pdf") {
        return "application/pdf";
    }

    if (ext == "wasm") {
        return "application/wasm";
    }

    if (ext == "webmanifest") {
        return "application/manifest+json";
    }

    if (ext == "mp4") {
        return "video/mp4";
    }

    return "application/octet-stream";
}

bool normalize_url_prefix(const char *input, std::string &output) {
    if (input == nullptr) {
        return false;
    }

    output = input;

    if (output.empty()) {
        output = "/";
    }

    if (output.front() != '/' || output.find('?') != std::string::npos ||
        output.find('#') != std::string::npos ||
        output.find('<') != std::string::npos ||
        output.find('>') != std::string::npos ||
        output.find('\\') != std::string::npos) {
        return false;
    }

    while (output.size() > 1 && output.back() == '/') {
        output.pop_back();
    }

    size_t pos = 1;

    while (pos < output.size()) {
        const size_t end = output.find('/', pos);

        const std::string segment = output.substr(
            pos, end == std::string::npos ? std::string::npos : end - pos);

        if (segment.empty() || segment == "." || segment == "..") {
            return false;
        }

        if (end == std::string::npos) {
            break;
        }

        pos = end + 1;
    }

    return true;
}

bool normalize_filesystem_root(const char *input, std::string &output) {
    if (input == nullptr) {
        return false;
    }

    output = input;

    if (output.empty() || output.front() != '/') {
        return false;
    }

    while (output.size() > 1 && output.back() == '/') {
        output.pop_back();
    }

    if (output.find('\\') != std::string::npos) {
        return false;
    }

    size_t pos = 1;

    while (pos < output.size()) {
        const size_t end = output.find('/', pos);

        const std::string segment = output.substr(
            pos, end == std::string::npos ? std::string::npos : end - pos);

        if (segment.empty() || segment == "." || segment == "..") {
            return false;
        }

        if (end == std::string::npos) {
            break;
        }

        pos = end + 1;
    }

    struct stat st {};

    errno = 0;

    const int result = stat(output.c_str(), &st);

    ESP_LOGI("StaticFiles", "Filesystem root: %s, stat=%d, errno=%d, is_dir=%d",
        output.c_str(), result, errno, result == 0 && S_ISDIR(st.st_mode));

    return result == 0 && S_ISDIR(st.st_mode);
}

} // namespace

esp_err_t WebServer::static_files(const char *url_prefix,
    const char *filesystem_root, const StaticFilesConfig &config) {
    std::string prefix;

    if (!normalize_url_prefix(url_prefix, prefix)) {
        return ESP_ERR_INVALID_ARG;
    }

    std::string root;

    if (!normalize_filesystem_root(filesystem_root, root)) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!is_safe_relative_path(config.index_file)) {
        return ESP_ERR_INVALID_ARG;
    }

    if (config.cache_control.empty()) {
        return ESP_ERR_INVALID_ARG;
    }

    if (m_server != nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    const std::string pattern =
        prefix == "/" ? "/<path...>" : prefix + "/<path...>";

    // 避免部分注册
    for (const auto &existing : m_routes) {
        if (existing.pattern == pattern &&
            (existing.method == HTTP_GET || existing.method == HTTP_HEAD)) {
            return ESP_ERR_INVALID_STATE;
        }
    }

    // 静态文件 Handler
    Handler handler = [root, config](const HttpRequest &req) -> HttpResponse {
        // 获取相对路径
        std::string relative = req.param("path");

        if (relative.empty()) {
            relative = config.index_file;
        }

        if (!is_safe_relative_path(relative)) {
            return HttpResponse::error(400, "Invalid file path");
        }

        // 获取文件状态
        std::string file_path = join_path(root, relative);

        struct stat st {};

        bool exists = stat(file_path.c_str(), &st) == 0;

        // 目录请求映射到 index.html
        if (exists && S_ISDIR(st.st_mode)) {
            relative += "/" + config.index_file;

            file_path = join_path(root, relative);

            exists = stat(file_path.c_str(), &st) == 0;
        }

        // SPA 回退
        if (!exists && config.spa_fallback && is_extensionless(relative)) {

            relative = config.index_file;

            file_path = join_path(root, relative);

            exists = stat(file_path.c_str(), &st) == 0;
        }

        // 文件检查
        if (!exists || !S_ISREG(st.st_mode)) {
            return HttpResponse::error(404, "Static file not found");
        }

        // MIME
        const std::string mime = get_mime_type(file_path);

        // HEAD 请求
        if (req.method() == HTTP_HEAD) {
            HttpResponse response = HttpResponse::text("", 200, mime.c_str());

            const std::string content_length =
                std::to_string(static_cast<unsigned long long>(st.st_size));

            response.header("Content-Length", content_length.c_str());
            response.header("Cache-Control", config.cache_control.c_str());
            response.header("X-Content-Type-config", "nosniff");

            return response;
        }

        // 打开文件
        auto file = std::shared_ptr<FILE>(
            std::fopen(file_path.c_str(), "rb"), [](FILE *fp) {
                if (fp != nullptr) {
                    std::fclose(fp);
                }
            });

        if (!file) {
            return HttpResponse::error(404, "Failed to open static file");
        }

        // 创建流式响应
        HttpResponse response = HttpResponse::stream(
            mime, [file](httpd_req_t *native) -> esp_err_t {
                constexpr size_t kChunkSize = 1024;
                char buffer[kChunkSize];

                while (true) {
                    const size_t bytes =
                        std::fread(buffer, 1, sizeof(buffer), file.get());

                    if (bytes > 0) {
                        const esp_err_t err = httpd_resp_send_chunk(
                            native, buffer, static_cast<ssize_t>(bytes));

                        if (err != ESP_OK) {
                            return err;
                        }
                    }

                    if (bytes < sizeof(buffer)) {
                        if (std::ferror(file.get())) {
                            return ESP_FAIL;
                        }

                        break;
                    }
                }

                // 结束 HTTP Chunked Response
                return httpd_resp_send_chunk(native, nullptr, 0);
            });

        response.header("Cache-Control", config.cache_control.c_str());
        response.header("X-Content-Type-config", "nosniff");

        return response;
    };

    ESP_LOGI("StaticFiles", "Register GET: %s", pattern.c_str());

    esp_err_t err = route(pattern.c_str(), HTTP_GET, handler);

    if (err != ESP_OK) {
        ESP_LOGE("StaticFiles", "GET registration failed: 0x%x",
            static_cast<unsigned>(err));

        return err;
    }

    ESP_LOGI("StaticFiles", "Register HEAD: %s", pattern.c_str());

    err = route(pattern.c_str(), HTTP_HEAD, handler);

    if (err != ESP_OK) {
        m_routes.pop_back();

        ESP_LOGE("StaticFiles", "HEAD registration failed: 0x%x",
            static_cast<unsigned>(err));

        return err;
    }

    ESP_LOGI("StaticFiles", "Static files registered: %s -> %s", prefix.c_str(),
        root.c_str());

    return ESP_OK;
}
