// OpenQuatt FE - DHW-runtime: toestandsmachine, niveaus en de naad naar het regelhart.
//
// Tikt oq_dhw::Controller, voedt oq_fe_dhw_bridge (CM10, niveaus, flow-setpoint) en
// oq_fe_io_runtime (klep en element). Port van de tick in oq_boiler_control.yaml van
// de LilyGO-build (v0.65.0), zonder de tarief-, PV- en leerfuncties (fase 4c).
#pragma once

#include <algorithm>

#include "oq_dhw_controller_logic.h"
#include "oq_fe_dhw_bridge.h"
#include "oq_fe_dhw_levels_logic.h"
#include "oq_fe_io_runtime.h"

// De map-include neemt deze header in elk target op; alleen het FE-target heeft
// de entiteiten waar de runtime naar verwijst.
#if OQ_FE_TARGET
namespace oq_fe_dhw_runtime {

constexpr int64_t LEGIONELLA_INTERVAL_S = 7LL * 24LL * 60LL * 60LL;

class Runtime {
 public:
  void clear_fault() {
    controller_.clear_fault_latch();
    ESP_LOGI("fe.dhw", "DHW fault cleared");
  }

  bool idle() const { return idle_; }

  void tick() {
    const uint32_t now_ms = (uint32_t)millis();
    const uint32_t dt_ms = last_tick_ms_ == 0 ? 0 : std::min<uint32_t>(now_ms - last_tick_ms_, 60000U);
    last_tick_ms_ = now_ms;

    seed_legionella_();

    // Service (CM100) of een override gaat voor: de toestandsmachine pauzeert
    // en alles valt veilig af, net als element-only in de LilyGO-build.
    const bool service = id(oq_commissioning_active) || id(oq_commissioning_request_pending) ||
                         id(oq_cm_override).active_index().value_or(0) != 0;
    if (service) {
      pause_();
      publish_();
      return;
    }

    const oq_dhw::Config cfg = config_();
    oq_dhw::Inputs in = inputs_(now_ms);
    const oq_dhw::Outputs out = controller_.tick(in, cfg);
    last_out_ = out;
    idle_ = out.state == oq_dhw::State::IDLE_CV && out.fault == oq_dhw::Fault::NONE;

    // Klep en element. Het element mag alleen op een bevestigde DHW-stand,
    // behalve in de element-only-fase van legionella (klep bewust op CV).
    const bool valve_dhw_confirmed = !in.valve_feedback_valid || !in.valve_feedback_cv;
    const bool legionella_element_only = out.element_on && !out.block_cv_priority;
    const bool element = out.element_on && (valve_dhw_confirmed || legionella_element_only);
    oq_fe_io_runtime::runtime().set_dhw_requests(out.valve_to_boiler, element);
    oq_fe_io_runtime::runtime().set_tests_allowed(idle_);

    update_levels_(now_ms, dt_ms, out);
    track_legionella_(out);
    publish_();
  }

 private:
  oq_dhw::Config config_() const {
    oq_dhw::Config cfg;
    cfg.start_top_c = id(oq_dhw_start_top_c).state;
    cfg.hp_stop_top_c = id(oq_dhw_hp_stop_top_c).state;
    cfg.boost_target_c = id(oq_dhw_boost_target_c).state;
    cfg.max_boost_target_c = id(oq_dhw_max_boost_target_c).state;
    cfg.max_boost_hp_stop_top_c = id(oq_dhw_max_boost_hp_stop_top_c).state;
    cfg.legionella_target_c = id(oq_dhw_legionella_target_c).state;
    cfg.legionella_hp_stop_top_c = id(oq_dhw_legionella_hp_stop_top_c).state;
    cfg.legionella_hp_top_ceiling_c = id(oq_dhw_legionella_hp_top_ceiling_c).state;
    // Vast, zoals in de LilyGO-build: de HP-fase stopt op tank bottom 52 C en
    // mag 180 min duren (220 L van koud naar 52 C kost ~100 min).
    cfg.stop_on_bottom_enable = true;
    cfg.hp_stop_bottom_c = 52.0f;
    cfg.hp_max_runtime_ms = 180UL * 60UL * 1000UL;
    cfg.legionella_use_coil_circulation = id(oq_dhw_legionella_coil_circulation_enable).state;
    cfg.hp_target_flow_c = id(oq_dhw_hp_target_flow_c).state;
    cfg.enable_boost_after_hp = id(oq_dhw_boost_after_hp).state;
    cfg.boost_hp_assist_enable = id(oq_dhw_boost_hp_assist_enable).state;
    cfg.boost_hp_assist_bottom_c = id(oq_dhw_boost_hp_assist_bottom_c).state;
    cfg.boost_hp_assist_stop_top_c = id(oq_dhw_boost_hp_assist_stop_top_c).state;
    cfg.min_cycle_rest_ms = (uint32_t)(std::max(0.0f, id(oq_dhw_min_rest_s).state) * 1000.0f);
    const float flow_min = id(oq_dhw_flow_min_lph).state;
    const float flow_max = id(oq_dhw_flow_max_lph).state;
    cfg.flow_min_lph = fminf(flow_min, flow_max);
    cfg.flow_max_lph = fmaxf(flow_min, flow_max);
    return cfg;
  }

  oq_dhw::Inputs inputs_(uint32_t now_ms) const {
    oq_dhw::Inputs in;
    in.now_ms = now_ms;
    in.tank_top_c = id(fe_dhw_tank_top).state;
    in.tank_bottom_c = id(fe_dhw_tank_bottom).state;
    in.coil_in_c = NAN;  // geen coil-sensoren op de Q
    in.coil_out_c = NAN;
    in.valve_feedback_valid = id(fe_dhw_valve_feedback).has_state();
    // Het hulpcontact sluit in de DHW-stand; de logica verwacht "CV".
    in.valve_feedback_cv = !id(fe_dhw_valve_feedback).state;
    in.flow_valid = id(flow_rate_selected).has_state() && !isnan(id(flow_rate_selected).state);
    in.flow_lph = in.flow_valid ? id(flow_rate_selected).state : NAN;
    in.hp_fault_active = all_hps_faulted_() || id(oq_water_temp_hard_trip_active);
    in.lockout_active = id(oq_dhw_lockout).state;
    in.start_inhibit = window_inhibit_();
    in.solar_boost_request = false;  // fase 4c
    in.max_boost_request = id(oq_dhw_boost_now).state;
    in.hp_thermal_limit_active = guards_.boost_limit;
    in.legionella_force_request = id(oq_dhw_src_legionella_force).state;
    in.legionella_seeded = legionella_seeded_;
    return in;
  }

  static bool all_hps_faulted_() {
    const uint8_t configured = id(oq_incident_manager).configured_hp_count();
    if (configured == 0) return false;
    uint8_t faulted = 0;
    for (uint8_t hp = 1; hp <= 2; ++hp) {
      if (!id(oq_incident_manager).hp_configured(hp)) continue;
      if (id(oq_incident_manager).get_outputs(hp).fault_active) faulted++;
    }
    return faulted >= configured;
  }

  static bool window_inhibit_() {
    if (!id(oq_dhw_window_enable).state) return false;
    const auto now = id(oq_time).now();
    if (!now.is_valid()) return true;
    const int h = now.hour;
    const int start_h = (int)id(oq_dhw_window_start_h).state;
    const int end_h = (int)id(oq_dhw_window_end_h).state;
    if (start_h <= end_h) return h < start_h || h >= end_h;
    return h < start_h && h >= end_h;  // over middernacht, bijv. 22-06
  }

  // Zet het legionella-tijdstip na een herstart terug uit de wandklok.
  void seed_legionella_() {
    if (legionella_seeded_) return;
    const auto now = id(oq_time).now();
    if (!now.is_valid()) return;  // zonder NTP blijft de planning geblokkeerd
    const int64_t last_done = id(oq_dhw_legionella_last_done_epoch_s);
    if (last_done > 0) {
      const int64_t age_s = (int64_t)now.timestamp - last_done;
      if (age_s >= 0) {
        const uint32_t now_ms = (uint32_t)millis();
        if (age_s >= LEGIONELLA_INTERVAL_S) {
          controller_.seed_legionella_last_done_ms(1);  // achterstallig: meteen
        } else if (age_s * 1000LL < (int64_t)now_ms) {
          const uint32_t seed = now_ms - (uint32_t)(age_s * 1000LL);
          controller_.seed_legionella_last_done_ms(seed > 0 ? seed : 1);
        } else {
          // Ouder dan de uptime: behandelen als "net gedaan". Iets later dan
          // ideaal, maar nooit een onterechte run vlak na een herstart.
          controller_.seed_legionella_last_done_ms(now_ms > 0 ? now_ms : 1);
        }
      }
    }
    legionella_seeded_ = true;
  }

  void track_legionella_(const oq_dhw::Outputs& out) {
    const int state = (int)out.state;
    const bool completed = prev_state_ == (int)oq_dhw::State::LEGIONELLA && state == (int)oq_dhw::State::IDLE_CV &&
                           out.fault == oq_dhw::Fault::NONE;
    if (completed) {
      const auto now = id(oq_time).now();
      if (now.is_valid()) {
        id(oq_dhw_legionella_last_done_epoch_s) = (int64_t)now.timestamp;
        ESP_LOGI("fe.dhw", "Legionella run completed");
      }
    }
    prev_state_ = state;
  }

  void update_levels_(uint32_t now_ms, uint32_t dt_ms, const oq_dhw::Outputs& out) {
    namespace lv = oq_fe_dhw_levels;
    const bool hp_request = out.hp_dhw_request;
    const int base_level = lv::clamp_int((int)roundf(id(oq_dhw_hp_level).state), 1, 10);

    // Mapping op de inlaat van de spiraal = uitlaat van de laatste HP.
    if (id(oq_dhw_coil_map_enable).state && hp_request) {
      const lv::MapConfig map{id(oq_dhw_coil_map_t1_c).state, id(oq_dhw_coil_map_t2_c).state,
                              id(oq_dhw_coil_map_t3_c).state, id(oq_dhw_coil_map_hyst_c).state};
      map_level_ = lv::map_level(map_level_, inlet_c_(), map, base_level);
    } else {
      map_level_ = base_level;
    }
    const uint32_t step_ms = (uint32_t)(std::max(0.0f, id(oq_dhw_softstart_step_min).state) * 60000.0f);
    const int ss_cap = lv::update_soft_start(soft_start_, hp_request, now_ms, step_ms);
    const int requested = lv::apply_cap(map_level_, ss_cap);
    requested_level_ = requested;

    const lv::GuardConfig guard_cfg{id(oq_dhw_assist_discharge_max_c).state, id(oq_dhw_assist_water_out_max_c).state};
    lv::GuardInputs gi;
    gi.discharge_hp1_c = id(hp1_gas_discharge_temp).state;
    gi.water_out_hp1_c = id(hp1_water_out_temp).state;
#if OQ_TOPOLOGY_DUO
    gi.discharge_hp2_c = id(hp2_gas_discharge_temp).state;
    gi.water_out_hp2_c = id(hp2_water_out_temp).state;
#endif
    gi.assist_is_hp2 = lead_ != 2;
    const auto trip = lv::update_guards(guards_, gi, guard_cfg);

    const bool single = id(oq_dhw_single_hp_mode).state;
    const bool max_boost = controller_.max_boost_active();
    int assist = 0;
#if OQ_TOPOLOGY_DUO
    if (single && !max_boost && hp_request) {
      const auto o1 = id(oq_incident_manager).get_outputs(1);
      const auto o2 = id(oq_incident_manager).get_outputs(2);
      const bool lead_is_hp1 = id(hp1_minutes) <= id(hp2_minutes);
      lead_ = lv::choose_lead(lead_, o1.fault_active, o2.fault_active, o1.protection_active, o2.protection_active,
                              lead_is_hp1);
    } else {
      lead_ = 0;  // lock loslaten buiten de HP-fase
    }
    const float rise = lv::update_rise(rise_, now_ms, id(fe_dhw_tank_top).state);
    lv::AssistInputs ai;
    ai.now_ms = now_ms;
    ai.dt_ms = dt_ms;
    ai.context = id(oq_dhw_assist_enable).state && single && hp_request && out.state == oq_dhw::State::DHW_HEAT_PUMP;
    ai.guard = guards_.assist_guard;
    ai.trip = trip;
    ai.tank_bottom_c = id(fe_dhw_tank_bottom).state;
    ai.water_out_assist_c = gi.assist_is_hp2 ? gi.water_out_hp2_c : gi.water_out_hp1_c;
    ai.saturated = requested >= base_level;
    ai.rise_k_min = rise;
    const lv::AssistConfig assist_cfg{
        (int)roundf(id(oq_dhw_assist_max_level).state),
        id(oq_dhw_assist_min_rise_k_min).state,
        id(oq_dhw_assist_bottom_stop_c).state,
        id(oq_dhw_assist_water_release_c).state,
        guard_cfg.water_out_max_c,
    };
    assist = lv::update_assist(assist_, ai, assist_cfg);
    if (!ai.context) lv::reset_rise(rise_);
#else
    (void)trip;
    (void)dt_ms;
    lead_ = 0;
#endif

    lv::LevelInputs li;
    li.duo = OQ_TOPOLOGY_DUO;
    li.hp_request = hp_request;
    li.level = requested;
    li.single_mode = single;
    li.max_boost = max_boost;
    li.lead = lead_;
    li.bump = (int)roundf(id(oq_dhw_single_hp_level_bump).state);
    li.soft_start_cap = ss_cap;
    li.assist_level = assist;
    const auto levels = lv::compute_levels(li);

    auto& bridge = oq_fe_dhw_bridge::state();
    bridge.mode_requested = out.hp_dhw_request || out.block_cv_priority;
    bridge.hp1_level = levels.hp1;
    bridge.hp2_level = levels.hp2;
    bridge.owner_hp = levels.owner;
    bridge.reason = levels.reason;
    bridge.flow_setpoint_lph = id(oq_flow_setpoint_dhw_lph).state;
  }

  static float inlet_c_() {
#if OQ_TOPOLOGY_DUO
    return id(hp2_water_out_temp).state;
#else
    return id(hp1_water_out_temp).state;
#endif
  }

  void pause_() {
    oq_fe_dhw_bridge::clear();
    oq_fe_io_runtime::runtime().set_dhw_requests(false, false);
    oq_fe_io_runtime::runtime().set_tests_allowed(false);
    oq_fe_dhw_levels::drop_assist(assist_, (uint32_t)millis(), false, 0);
    soft_start_ = oq_fe_dhw_levels::SoftStart{};
    lead_ = 0;
    idle_ = false;
  }

  void publish_() {
    const auto& out = last_out_;
    publish_text_(id(oq_dhw_state_text), state_text_, oq_dhw::state_name(out.state));
    publish_text_(id(oq_dhw_fault_text), fault_text_, oq_dhw::fault_name(out.fault));
    publish_bool_(id(oq_dhw_hp_request_active_sensor), out.hp_dhw_request);
    publish_bool_(id(oq_dhw_block_cv_priority_sensor), out.block_cv_priority);
    const auto& bridge = oq_fe_dhw_bridge::state();
    if (bridge.reason != level_reason_) {
      id(oq_dhw_level_reason).publish_state(bridge.reason);
      level_reason_ = bridge.reason;
    }
  }

  static void publish_text_(text_sensor::TextSensor& sensor, const char*& last, const char* text) {
    if (last == text) return;
    sensor.publish_state(text);
    last = text;
  }

  static void publish_bool_(binary_sensor::BinarySensor& sensor, bool value) {
    if (!sensor.has_state() || sensor.state != value) sensor.publish_state(value);
  }

  oq_dhw::Controller controller_{};
  oq_dhw::Outputs last_out_{};
  bool legionella_seeded_ = false;
  bool idle_ = false;
  int prev_state_ = (int)oq_dhw::State::IDLE_CV;
  uint32_t last_tick_ms_ = 0;

  int map_level_ = 4;
  int requested_level_ = 0;
  int lead_ = 0;
  oq_fe_dhw_levels::SoftStart soft_start_{};
  oq_fe_dhw_levels::GuardState guards_{};
  oq_fe_dhw_levels::AssistState assist_{};
  oq_fe_dhw_levels::RiseTracker rise_{};

  const char* state_text_ = nullptr;
  const char* fault_text_ = nullptr;
  const char* level_reason_ = nullptr;

 public:
  int mapped_level() const { return map_level_; }
  int requested_level() const { return requested_level_; }
  int assist_level() const { return assist_.level; }
  int lead() const { return lead_; }
  float target_flow_c() const { return last_out_.target_flow_temp_c; }
};

inline Runtime& runtime() {
  static Runtime value;
  return value;
}

}  // namespace oq_fe_dhw_runtime
#endif
