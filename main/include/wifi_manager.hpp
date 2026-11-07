#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

namespace WiFiManager {

inline constexpr EventBits_t kOnline = BIT0;
inline constexpr EventBits_t kFailed = BIT1;

// 初始化 Wi-Fi STA
esp_err_t init();

// 等待联网成功或重连失败
EventBits_t wait();

// 查询当前是否在线
bool is_online();

} // namespace WiFiManager
