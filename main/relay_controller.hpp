#pragma once

#include <array>
#include <cstddef>
#include <mutex>

#include "esp_err.h"
#include "soc/gpio_num.h"

class RelayController {
public:
  static constexpr std::array<gpio_num_t, 4> pins{GPIO_NUM_23, GPIO_NUM_24,
                                                  GPIO_NUM_25, GPIO_NUM_26};
  struct State {
    bool online;
    std::array<bool, 4> on;
  };

  esp_err_t init();
  esp_err_t set(std::size_t channel, bool on);
  esp_err_t all_off();
  // 断网时，在同一个锁内禁止新指令并释放全部继电器。
  esp_err_t set_online(bool online);
  State state() const;

private:
  esp_err_t all_off_locked();
  mutable std::mutex mutex_;
  bool online_ = false;
  std::array<bool, 4> on_{};
};
