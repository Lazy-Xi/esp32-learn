# ESP32 连接 Wi-Fi 并提供 HTML 网页

这是基于 ESP-IDF Wi-Fi Station 示例的最小 HTTP 项目。当前项目配置使用 **ESP32-C5 + ESP-IDF 6.1 + GNU C++26**（`-std=gnu++26`）。入口为 `main/main.cpp`，通过 `extern "C"` 导出 `app_main()`。

当前开发板检测为 **ESP32-C5HF4，芯片版本 v1.0，内置 4 MB Flash，48 MHz 晶振**。`sdkconfig.defaults` 固定目标为 ESP32-C5、Flash 容量为 4 MB（DIO、80 MHz）、CPU 默认频率为 240 MHz，最低兼容芯片版本为 v1.0，PSRAM 默认关闭。晶振使用 ESP-IDF 的自动识别模式，主控制台通过 USB Serial/JTAG 输出，当前串口为 `COM4`。

启动流程：初始化 NVS → 连接路由器 Wi-Fi → 获取 IP 地址 → 启动 80 端口 HTTP 服务 → 浏览器访问 `/`，显示 HTML 页面。

## 1. 创建项目

如果从零开始，在 VS Code 的 ESP-IDF 扩展中运行 `ESP-IDF: New Project`，选择 `wifi/getting_started/station` 示例，并选择开发板对应的芯片。当前目录已经完成这一步，并加入了 HTTP 服务，可以直接继续配置。

也可以复制 ESP-IDF 安装目录中的 `examples/wifi/getting_started/station` 文件夹作为新项目，再按本项目的方式增加 HTTP 服务。

## 2. 配置 Wi-Fi

VS Code 已配置默认终端 `ESP-IDF PowerShell`，新建终端会自动加载本机安装的 ESP-IDF 6.1 环境。已有终端需要重新新建，或运行 `ESP-IDF: Open ESP-IDF Terminal`。

在已经加载 ESP-IDF 环境的终端中运行：

```powershell
idf.py menuconfig
```

进入 `Example Configuration`，设置 `WiFi SSID` 和 `WiFi Password`，保存退出。已有的 Wi-Fi 配置会继续使用。

如果开发板不是 ESP32-C5，先用 `idf.py set-target 你的芯片型号` 设置实际芯片，再配置 Wi-Fi。例如普通 ESP32 使用 `idf.py set-target esp32`。切换芯片会重新生成配置。

## 3. 编译、烧录并查看地址

```powershell
idf.py build
idf.py -p COM4 flash monitor
```

`COM4` 是当前工作区配置的串口，实际使用时按设备修改。退出串口监视器按 `Ctrl+]`。

Wi-Fi 获取 IP 并启动 HTTP 服务后，会输出类似：

```text
wifi station: got ip:192.168.1.123
wifi station: Open http://192.168.1.123/ in your browser
```

让电脑或手机连接与开发板相同的局域网，然后在浏览器输入串口中打印的 **`http://实际IP/`**。IP 由路由器分配，以日志为准；不要使用 HTTPS。

## 4. 修改网页

- `main/index.html`：网页内容，可以直接修改 HTML 和 CSS。
- `main/main.cpp`：连接 Wi-Fi、启动 HTTP 服务以及处理 `GET /`。
- `main/CMakeLists.txt`：声明 `esp_http_server` 等依赖，通过 `EMBED_TXTFILES` 将网页编译进固件。
- `main/Kconfig.projbuild`：定义 Wi-Fi 配置菜单。

收到 `IP_EVENT_STA_GOT_IP` 后，程序调用 `httpd_start()`，再用 `httpd_register_uri_handler()` 注册 `/`。浏览器发起 `GET /` 时，`root_get_handler()` 返回嵌入的网页，响应类型为 `text/html; charset=utf-8`。

网页保存在固件中，因此修改后需要重新编译和烧录，无需额外文件系统。Wi-Fi 断开时会停止 HTTP 服务，重连拿到 IP 后重新启动。连续连接失败达到 `Maximum retry` 后停止重试，需要检查配置并重启开发板。

## VS Code 开发环境

- 使用 Espressif clangd 进行 C++ 补全和诊断，读取 `build/compile_commands.json` 中的真实编译参数。
- clangd 已配置 Espressif Clang 的内置头文件目录，并从 ESP32 GCC 工具链提取 C/C++ 标准库路径；`.clangd` 只排除已确认不被 clangd 支持的 GCC 参数。
- CMake 显式要求 C++26，沿用 ESP-IDF 的 GNU 扩展。GCC 15.2 支持该语言模式，具体 C++26 特性以工具链已实现的内容为准。
- ESP-IDF 扩展负责配置、构建、烧录和串口监视；已关闭 CMake Tools 的自动配置，避免两套构建流程互相干扰。
- 修改配置后，运行 `Developer: Reload Window`，然后打开 `main/main.cpp`。编译数据库由 `idf.py build` 生成，不需要手工维护头文件搜索列表。

编译、烧录和串口监视使用 ESP-IDF 安装时自带的 Python 环境；项目无需额外创建 Python 虚拟环境或安装 Python 开发依赖。

## 无法访问时

Wi-Fi 驱动的 `(trc)band:...`、`<ba-add>` 等内部诊断可能以 `W` 级别输出；仅凭这些日志不能判断连接失败。项目在 Wi-Fi 初始化前调用 `esp_log_level_set("wifi", ESP_LOG_ERROR)`，仅输出 `wifi` 标签的错误，屏蔽该标签的警告和普通信息。应用的 `wifi station` 标签仍输出 IP、访问地址、断线原因 `reason`、重试次数和重试耗尽错误。需要排查驱动时，可在 `main/main.cpp` 中将 `ESP_LOG_ERROR` 改回 `ESP_LOG_INFO` 并重新编译烧录。参见[乐鑫 Wi-Fi FAQ](https://docs.espressif.com/projects/esp-faq/zh_CN/latest/software-framework/wifi.html)。

1. 确认串口已打印访问地址；如果 Wi-Fi 连接失败，检查名称、密码以及路由器支持的频段和认证方式。
2. 确认浏览器使用 `http://` 和开发板的实际 IP。
3. 确认手机或电脑与开发板在同一局域网，路由器没有开启访客网络隔离或 AP 隔离。

官方文档：[ESP-IDF HTTP Server](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c5/api-reference/protocols/esp_http_server.html)。
