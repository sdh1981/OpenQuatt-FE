// OpenQuatt FE - DHW-runtime: toestandsmachine, niveaus en de naad naar het regelhart.
//
// Tikt oq_dhw::Controller, voedt oq_fe_dhw_bridge (CM10, niveaus, flow-setpoint) en
// oq_fe_io_runtime (klep en element). Port van de tick in oq_boiler_control.yaml van
// de LilyGO-build (v0.65.0). Tarief/PV-sturing en adaptief leren vervallen; tapdetectie,
// de standby-loss-lerer, de ETA's en element-only (CM11) zijn er wel.
#pragma once

#include <algorithm>

#include "oq_dhw_controller_logic.h"
#include "oq_fe_dhw_bridge.h"
#include "oq_fe_dhw_levels_logic.h"
#include "oq_fe_dhw_tank_logic.h"
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

  void reset_learning() {
    id(oq_dhw_ua_tank_wpk) = 0.0f;
    id(oq_dhw_ua_tank_sample_count) = 0;
    learner_ = oq_fe_dhw_tank::LearnerState{};
    ESP_LOGI("fe.dhw", "DHW standby-loss learning reset");
  }

  // Ruimtetemperatuur rond de tank uit Home Assistant (on_value).
  void note_room_temp(float value_c) {
    room_ha_c_ = value_c;
    room_ha_ms_ = (uint32_t)millis();
  }

  // Ruimtetemperatuur voor de lerer en de ETA's: Home Assistant zolang die vers
  // is (< 30 min), anders het ingestelde getal.
  float ambient_c() const {
    const bool use_ha = id(oq_dhw_room_temp_source).current_option() == "Home Assistant";
    const bool fresh = room_ha_ms_ != 0 && (uint32_t)((uint32_t)millis() - room_ha_ms_) < 1800000UL;
    if (use_ha && fresh && !isnan(room_ha_c_)) return room_ha_c_;
    return id(oq_dhw_room_temp_c).state;
  }

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

    const bool element_only = id(oq_dhw_element_only_enable).state;
    const oq_dhw::Config cfg = config_();
    oq_dhw::Inputs in = inputs_(now_ms);
    if (element_only) {
      // Element-only: geen nieuwe cycli en geen snelboost. Legionella mag wel
      // (de start-inhibit geldt niet voor legionella), en een lopende cyclus
      // maakt eerst af.
      in.start_inhibit = true;
      in.max_boost_request = false;
    }
    const oq_dhw::Outputs out = controller_.tick(in, cfg);
    last_out_ = out;
    idle_ = out.state == oq_dhw::State::IDLE_CV && out.fault == oq_dhw::Fault::NONE;

    // Element-only-thermostaat op tank bottom, alleen in CM11 zelf.
    const bool in_cm11 = id(oq_control_mode_code) == oq_fe_dhw_bridge::CM_ELEMENT_ONLY;
    eo_on_ = in_cm11 && oq_fe_dhw_tank::element_only_on(eo_on_, !was_cm11_, id(fe_dhw_tank_bottom).state,
                                                        id(oq_dhw_element_only_target_c).state,
                                                        id(oq_dhw_element_only_off_delta_c).state);
    was_cm11_ = in_cm11;

    // Klep en element. Het element mag alleen op een bevestigde DHW-stand,
    // behalve in de element-only-fase van legionella (klep bewust op CV). In
    // CM11 vraagt ook de thermostaat om het element; de klep blijft dan op CV.
    const bool valve_dhw_confirmed = !in.valve_feedback_valid || !in.valve_feedback_cv;
    const bool legionella_element_only = out.element_on && !out.block_cv_priority;
    const bool fsm_element = out.element_on && (valve_dhw_confirmed || legionella_element_only);
    oq_fe_io_runtime::runtime().set_dhw_requests(out.valve_to_boiler, fsm_element || eo_on_);
    oq_fe_io_runtime::runtime().set_tests_allowed(idle_ && !element_only);

    update_levels_(now_ms, dt_ms, out);
    oq_fe_dhw_bridge::state().element_only_requested = element_only;
    track_legionella_(out, now_ms);
    update_tank_model_(now_ms, out, fsm_element || eo_on_);
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

  void track_legionella_(const oq_dhw::Outputs& out, uint32_t now_ms) {
    const int state = (int)out.state;
    if (out.state == oq_dhw::State::LEGIONELLA) {
      // Zelfde meetpunt als de hold in de toestandsmachine: top, anders bottom.
      const float top = id(fe_dhw_tank_top).state;
      const float check = !isnan(top) ? top : id(fe_dhw_tank_bottom).state;
      if (hold_start_ms_ == 0 && !isnan(check) && check >= id(oq_dhw_legionella_target_c).state) {
        hold_start_ms_ = now_ms == 0 ? 1 : now_ms;
      }
    } else {
      hold_start_ms_ = 0;
    }
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
    eo_on_ = false;
    was_cm11_ = false;
  }

  void update_tank_model_(uint32_t now_ms, const oq_dhw::Outputs& out, bool element_on) {
    namespace tk = oq_fe_dhw_tank;
    if (!tank_loaded_) {
      tap_.taps_today = id(oq_dhw_taps_today);
      tap_.last_tap_wh = id(oq_dhw_last_tap_wh);
      tank_loaded_ = true;
    }
    const auto now = id(oq_time).now();
    if (now.is_valid()) {
      if (last_day_ >= 0 && now.day_of_year != last_day_) tap_.taps_today = 0;  // middernacht
      last_day_ = now.day_of_year;
    }
    const float top = id(fe_dhw_tank_top).state;
    const float volume = id(oq_dhw_tank_volume_l).state;
    const bool heating = out.hp_dhw_request || element_on;
    tk::TapConfig tap_cfg;
    tap_cfg.threshold_k_min = id(oq_dhw_tap_rate_threshold).state;
    tap_cfg.volume_l = volume;
    tk::update_tap(tap_, id(oq_dhw_tap_detect_enable).state, heating, top, now_ms, tap_cfg);
    id(oq_dhw_taps_today) = tap_.taps_today;
    id(oq_dhw_last_tap_wh) = tap_.last_tap_wh;

    const float ambient = this->ambient_c();
    tk::LearnerConfig learn_cfg;
    learn_cfg.volume_l = volume;
    learn_cfg.ambient_c = ambient;
    float ua = id(oq_dhw_ua_tank_wpk);
    int samples = id(oq_dhw_ua_tank_sample_count);
    const bool rest = out.state == oq_dhw::State::IDLE_CV && !element_on;
    if (tk::update_learner(learner_, ua, samples, rest, top, now_ms, tap_, learn_cfg)) {
      id(oq_dhw_ua_tank_wpk) = ua;
      id(oq_dhw_ua_tank_sample_count) = samples;
      ESP_LOGI("fe.dhw", "Standby loss sample: UA %.2f W/K (%d samples)", ua, samples);
    }

    float hp_power = 0.0f;
    const float p1 = id(hp1_heat_power).state;
    if (!isnan(p1)) hp_power += p1;
#if OQ_TOPOLOGY_DUO
    const float p2 = id(hp2_heat_power).state;
    if (!isnan(p2)) hp_power += p2;
#endif
    eta_ready_min_ = tk::time_to_ready_min(heating, top, id(oq_dhw_hp_stop_top_c).state, volume, hp_power, element_on,
                                           id(oq_dhw_ua_tank_wpk), ambient);
    if (out.state == oq_dhw::State::LEGIONELLA) {
      tk::LegionellaEtaInputs li;
      li.top_c = top;
      li.target_c = id(oq_dhw_legionella_target_c).state;
      li.hp_top_ceiling_c = id(oq_dhw_legionella_hp_top_ceiling_c).state;
      li.volume_l = volume;
      li.hp_phase_active = out.hp_dhw_request;
      li.hp_power_w = hp_power;
      li.hold_started = hold_start_ms_ != 0;
      li.hold_elapsed_ms = hold_start_ms_ != 0 ? (uint32_t)(now_ms - hold_start_ms_) : 0;
      li.ua = id(oq_dhw_ua_tank_wpk);
      li.ambient_c = ambient;
      eta_legionella_min_ = tk::legionella_eta_min(li);
    } else {
      eta_legionella_min_ = NAN;
    }
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

  bool eo_on_ = false;
  bool was_cm11_ = false;
  uint32_t hold_start_ms_ = 0;
  oq_fe_dhw_tank::TapState tap_{};
  oq_fe_dhw_tank::LearnerState learner_{};
  bool tank_loaded_ = false;
  int last_day_ = -1;
  float room_ha_c_ = NAN;
  uint32_t room_ha_ms_ = 0;
  float eta_ready_min_ = NAN;
  float eta_legionella_min_ = NAN;

  const char* state_text_ = nullptr;
  const char* fault_text_ = nullptr;
  const char* level_reason_ = nullptr;

 public:
  int mapped_level() const { return map_level_; }
  int requested_level() const { return requested_level_; }
  int assist_level() const { return assist_.level; }
  int lead() const { return lead_; }
  float target_flow_c() const { return last_out_.target_flow_temp_c; }
  bool element_only_on() const { return eo_on_; }
  bool tap_active() const { return tap_.active; }
  float tap_rate_k_min() const { return tap_.rate_k_min; }
  float eta_ready_min() const { return eta_ready_min_; }
  float eta_legionella_min() const { return eta_legionella_min_; }
};

inline Runtime& runtime() {
  static Runtime value;
  return value;
}

}  // namespace oq_fe_dhw_runtime
#endif
