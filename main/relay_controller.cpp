#include "relay_controller.hpp"

#include <cstdint>

#include "driver/gpio.h"
#include "sdkconfig.h"

namespace {
#ifdef CONFIG_RELAY_ACTIVE_LOW
constexpr bool kActiveLow = true;
#else
constexpr bool kActiveLow = false;
#endif
constexpr std::uint32_t level(bool on) {
  return (kActiveLow ? !on : on) ? 1U : 0U;
}
} // namespace

esp_err_t RelayController::init() {
  const std::lock_guard lock(mutex_);
  gpio_config_t config{};
  config.mode = GPIO_MODE_OUTPUT;
  config.pull_up_en = kActiveLow ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE;
  config.pull_down_en =
      kActiveLow ? GPIO_PULLDOWN_DISABLE : GPIO_PULLDOWN_ENABLE;
  config.intr_type = GPIO_INTR_DISABLE;
  for (const auto pin : pins) {
    const auto err = gpio_set_level(pin, level(false));
    if (err != ESP_OK)
      return err;
    config.pin_bit_mask |= (std::uint64_t{1} << pin);
  }
  const auto err = gpio_config(&config);
  return err == ESP_OK ? all_off_locked() : err;
}

esp_err_t RelayController::set(std::size_t channel, bool on) {
  if (channel < 1 || channel > pins.size())
    return ESP_ERR_INVALID_ARG;
  const std::lock_guard lock(mutex_);
  if (!online_)
    return ESP_ERR_INVALID_STATE;
  const auto err = gpio_set_level(pins[channel - 1], level(on));
  if (err == ESP_OK)
    on_[channel - 1] = on;
  return err;
}

esp_err_t RelayController::all_off_locked() {
  esp_err_t result = ESP_OK;
  for (std::size_t i = 0; i < pins.size(); ++i) {
    const auto err = gpio_set_level(pins[i], level(false));
    if (err == ESP_OK) {
      on_[i] = false;
    } else if (result == ESP_OK) {
      result = err; // 一路失败也继续尝试关闭其余通道。
    }
  }
  return result;
}

esp_err_t RelayController::all_off() {
  const std::lock_guard lock(mutex_);
  return all_off_locked();
}

esp_err_t RelayController::set_online(bool online) {
  const std::lock_guard lock(mutex_);
  online_ = online;
  return online ? ESP_OK : all_off_locked();
}

RelayController::State RelayController::state() const {
  const std::lock_guard lock(mutex_);
  return {online_, on_};
}
