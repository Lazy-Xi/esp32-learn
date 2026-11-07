#include "relay.hpp"

#include "driver/gpio.h"
#include "esp_log.h"
#include "sdkconfig.h"

namespace {

constexpr char kTag[] = "Relay";

constexpr uint8_t kRelayCount = CONFIG_RELAY_COUNT;

constexpr gpio_num_t kRelayPins[] = {
    static_cast<gpio_num_t>(CONFIG_RELAY_GPIO_0),
    static_cast<gpio_num_t>(CONFIG_RELAY_GPIO_1),
    static_cast<gpio_num_t>(CONFIG_RELAY_GPIO_2),
    static_cast<gpio_num_t>(CONFIG_RELAY_GPIO_3)};

#ifdef CONFIG_RELAY_ACTIVE_LOW
constexpr uint32_t kActiveLevel = 0;
#else
constexpr uint32_t kActiveLevel = 1;
#endif

constexpr uint32_t kInactiveLevel = !kActiveLevel;

bool relay_states[kRelayCount] {};

} // namespace

namespace Relay {

// ============================================================
// 初始化
// ============================================================

esp_err_t init() {
    for (uint8_t i = 0; i < kRelayCount; ++i) {

        esp_err_t err = gpio_reset_pin(kRelayPins[i]);

        if (err != ESP_OK) return err;

        err = gpio_set_direction(kRelayPins[i], GPIO_MODE_OUTPUT);

        if (err != ESP_OK) return err;

        err = gpio_set_level(kRelayPins[i], kInactiveLevel);

        if (err != ESP_OK) return err;

        relay_states[i] = false;

        ESP_LOGI(kTag, "Relay %u initialized, GPIO=%d",
            static_cast<unsigned>(i), static_cast<int>(kRelayPins[i]));
    }

    return ESP_OK;
}

// ============================================================
// 数量
// ============================================================

uint8_t count() { return kRelayCount; }

// ============================================================
// 获取状态
// ============================================================

esp_err_t get_state(uint8_t id, bool *state) {
    if (id >= kRelayCount || state == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    *state = relay_states[id];

    return ESP_OK;
}

// ============================================================
// 设置状态
// ============================================================

esp_err_t set_state(uint8_t id, bool state) {
    if (id >= kRelayCount) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err =
        gpio_set_level(kRelayPins[id], state ? kActiveLevel : kInactiveLevel);

    if (err != ESP_OK) return err;

    relay_states[id] = state;

    ESP_LOGI(
        kTag, "Relay %u: %s", static_cast<unsigned>(id), state ? "ON" : "OFF");

    return ESP_OK;
}

// ============================================================
// 全部关闭
// ============================================================

esp_err_t all_off() {
    for (uint8_t i = 0; i < kRelayCount; ++i) {

        esp_err_t err = set_state(i, false);

        if (err != ESP_OK) return err;
    }

    return ESP_OK;
}

} // namespace Relay
