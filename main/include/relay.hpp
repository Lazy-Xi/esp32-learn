#pragma once

#include <cstdint>

#include "esp_err.h"

namespace Relay {

esp_err_t init();

uint8_t count();

esp_err_t get_state(uint8_t id, bool *state);

esp_err_t set_state(uint8_t id, bool state);

esp_err_t all_off();

} // namespace Relay
