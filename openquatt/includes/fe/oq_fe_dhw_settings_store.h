// OpenQuatt FE - DHW-instellingen bewaren als één NVS-record (alleen in het
// FE-target). Zie oq_fe_dhw_settings_record.h en fe/nvs-voorstel.md, stap 2.
#pragma once

#include <array>
#include <cmath>

#include "oq_fe_dhw_settings_record.h"

// De map-include neemt deze header in elk target op; alleen het FE-target heeft
// de entiteiten waar de opslag naar verwijst.
#if OQ_FE_TARGET
namespace oq_fe_dhw_settings {

// Vaste NVS-sleutel van het record ("FEDS").
static constexpr uint32_t kPreferenceKey = 0x46454453u;
// Na de laatste wijziging zo lang wachten voor het opslaan: een back-up
// terugzetten of een reeks wijzigingen wordt zo één write.
static constexpr uint32_t kSettleMs = 5000;

class Store {
 public:
  // Vanuit de DHW-tick (elke 2 s). De eerste aanroep laadt het record vóór de
  // regeling de instellingen leest; daarna wordt elke wijziging bewaard.
  void tick(uint32_t now_ms) {
    if (!loaded_) {
      load_();
      loaded_ = true;
      return;
    }
    Record current = snapshot_();
    if (!timer_.update(!same_values(current, saved_), now_ms, kSettleMs)) return;
    seal(current);
    if (pref_.save(&current) && global_preferences->sync()) {
      saved_ = current;
      ESP_LOGI(TAG, "DHW settings saved");
    } else {
      ESP_LOGE(TAG, "DHW settings could not be saved (NVS full?); retrying");
      timer_.restart(now_ms);
    }
  }

 private:
  static constexpr const char* TAG = "fe.settings";

  // De volgorde ligt vast in het record. Wijzig je hem, verhoog dan kVersion.
  static std::array<number::Number*, kNumberCount> numbers_() {
    return {
        &id(oq_dhw_start_top_c),
        &id(oq_dhw_hp_stop_top_c),
        &id(oq_dhw_hp_level),
        &id(oq_flow_setpoint_dhw_lph),
        &id(oq_dhw_flow_min_lph),
        &id(oq_dhw_flow_max_lph),
        &id(oq_dhw_min_rest_s),
        &id(oq_dhw_hp_target_flow_c),
        &id(oq_dhw_boost_target_c),
        &id(oq_dhw_boost_hp_assist_bottom_c),
        &id(oq_dhw_boost_hp_assist_stop_top_c),
        &id(oq_dhw_max_boost_target_c),
        &id(oq_dhw_max_boost_hp_stop_top_c),
        &id(oq_dhw_legionella_target_c),
        &id(oq_dhw_legionella_hp_stop_top_c),
        &id(oq_dhw_legionella_hp_top_ceiling_c),
        &id(oq_dhw_window_start_h),
        &id(oq_dhw_window_end_h),
        &id(oq_dhw_coil_map_t1_c),
        &id(oq_dhw_coil_map_t2_c),
        &id(oq_dhw_coil_map_t3_c),
        &id(oq_dhw_coil_map_hyst_c),
        &id(oq_dhw_single_hp_level_bump),
        &id(oq_dhw_softstart_step_min),
        &id(oq_dhw_assist_max_level),
        &id(oq_dhw_assist_min_rise_k_min),
        &id(oq_dhw_assist_discharge_max_c),
        &id(oq_dhw_assist_water_out_max_c),
        &id(oq_dhw_assist_bottom_stop_c),
        &id(oq_dhw_assist_water_release_c),
        &id(oq_dhw_tap_rate_threshold),
        &id(oq_dhw_tank_volume_l),
        &id(oq_dhw_element_only_target_c),
        &id(oq_dhw_element_only_off_delta_c),
        &id(oq_dhw_room_temp_c),
    };
  }

  static std::array<switch_::Switch*, kSwitchCount> switches_() {
    return {
        &id(oq_dhw_lockout),
        &id(oq_dhw_boost_after_hp),
        &id(oq_dhw_boost_hp_assist_enable),
        &id(oq_dhw_window_enable),
        &id(oq_dhw_legionella_coil_circulation_enable),
        &id(oq_dhw_coil_map_enable),
        &id(oq_dhw_single_hp_mode),
        &id(oq_dhw_assist_enable),
        &id(oq_dhw_tap_detect_enable),
        &id(oq_dhw_element_only_enable),
    };
  }

  static select::Select* select_() { return &id(oq_dhw_room_temp_source); }

  Record snapshot_() const {
    Record record = empty_record();
    const auto numbers = numbers_();
    for (size_t i = 0; i < kNumberCount; ++i) record.numbers[i] = numbers[i]->state;
    const auto switches = switches_();
    for (size_t i = 0; i < kSwitchCount; ++i) set_switch_bit(record, i, switches[i]->state);
    const auto index = select_()->active_index();
    record.select = index.has_value() && *index < kNoSelect ? static_cast<uint8_t>(*index) : kNoSelect;
    return record;
  }

  void load_() {
    pref_ = global_preferences->make_preference<Record>(kPreferenceKey, true);
    Record record = empty_record();
    if (!pref_.load(&record) || !is_valid(record)) {
      // Geen of ongeldig record: de standaardwaarden blijven staan. Pas bij de
      // eerste wijziging wordt een record geschreven.
      ESP_LOGW(TAG, "No valid DHW settings record; using defaults");
      saved_ = snapshot_();
      return;
    }
    apply_(record);
    saved_ = snapshot_();
    ESP_LOGI(TAG, "DHW settings restored");
  }

  static void apply_(const Record& record) {
    const auto numbers = numbers_();
    for (size_t i = 0; i < kNumberCount; ++i) {
      const float value = record.numbers[i];
      number::Number* number = numbers[i];
      if (!std::isfinite(value) || value < number->traits.get_min_value() || value > number->traits.get_max_value()) {
        ESP_LOGW(TAG, "Stored value for %s out of range; keeping default", number->get_name().c_str());
        continue;
      }
      if (value != number->state) number->make_call().set_value(value).perform();
    }
    const auto switches = switches_();
    for (size_t i = 0; i < kSwitchCount; ++i) {
      const bool on = switch_bit(record, i);
      if (on != switches[i]->state) on ? switches[i]->turn_on() : switches[i]->turn_off();
    }
    select::Select* select = select_();
    if (record.select != kNoSelect && record.select < select->size()) {
      select->make_call().set_index(record.select).perform();
    }
  }

  ESPPreferenceObject pref_;
  Record saved_ = empty_record();
  SettleTimer timer_;
  bool loaded_ = false;
};

inline Store& store() {
  static Store instance;
  return instance;
}

}  // namespace oq_fe_dhw_settings
#endif  // OQ_FE_TARGET
