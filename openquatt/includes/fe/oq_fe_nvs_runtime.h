// OpenQuatt FE - NVS-bezetting meten (alleen in het FE-target).
#pragma once

#include "oq_fe_io_logic.h"

// De map-include neemt deze header in elk target op; de meting hoort bij FE.
#if OQ_FE_TARGET
#include "nvs.h"

namespace oq_fe_nvs {

static constexpr const char* TAG = "fe.nvs";

// Vrije entries in de standaard-NVS-partitie, of NAN als de meting faalt.
inline float measure_and_log() {
  nvs_stats_t stats{};
  const esp_err_t err = nvs_get_stats(nullptr, &stats);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "NVS stats unavailable: %s", esp_err_to_name(err));
    return NAN;
  }
  const int available = static_cast<int>(stats.available_entries);
  ESP_LOGI(TAG, "NVS: used=%u free=%u available=%d total=%u namespaces=%u", static_cast<unsigned>(stats.used_entries),
           static_cast<unsigned>(stats.free_entries), available, static_cast<unsigned>(stats.total_entries),
           static_cast<unsigned>(stats.namespace_count));
  switch (oq_fe_io::nvs_level(available)) {
    case oq_fe_io::NvsLevel::CRITICAL:
      ESP_LOGE(TAG, "NVS nearly full (%d entries free): changed settings may not survive a restart", available);
      break;
    case oq_fe_io::NvsLevel::LOW:
      ESP_LOGW(TAG, "NVS running low (%d entries free)", available);
      break;
    default:
      break;
  }
  return static_cast<float>(available);
}

}  // namespace oq_fe_nvs
#endif  // OQ_FE_TARGET
