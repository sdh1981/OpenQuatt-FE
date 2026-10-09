// OpenQuatt FE - compressorniveaus tijdens DHW.
//
// Port van de LilyGO-build (oq_boiler_control.yaml en oq_thermal_request_control.yaml,
// v0.65.0) naar pure logica. Gedrag en standaardwaarden zijn gelijk gebleven; één
// verschil: de niveaumapping leest de uitlaat van de laatste warmtepomp in plaats
// van de coil-in-sensor, die op de Q niet bestaat. Tijdens DHW is dat hetzelfde
// water dat de spiraal in gaat.
#pragma once

#include <math.h>
#include <stdint.h>

namespace oq_fe_dhw_levels {

inline int clamp_int(int value, int lo, int hi) { return value < lo ? lo : (value > hi ? hi : value); }

// ---------------------------------------------------------------------------
// Niveaumapping op de inlaattemperatuur van de spiraal (4 -> 3 -> 2 -> 1).
// ---------------------------------------------------------------------------
struct MapConfig {
  float t1_c = 40.0f;  // 4 -> 3
  float t2_c = 44.0f;  // 3 -> 2
  float t3_c = 48.0f;  // 2 -> 1
  float hysteresis_c = 0.5f;
};

// Volgende mappingstand vanuit `current`. Nooit onder 1 (nooit uit tijdens DHW),
// nooit boven `base_level`. Zonder geldige temperatuur blijft het basisniveau.
inline int map_level(int current, float inlet_c, const MapConfig& cfg, int base_level) {
  if (isnan(inlet_c)) return base_level;
  const float h = cfg.hysteresis_c;
  int mapped;
  if (current >= 4) {
    mapped = (inlet_c >= cfg.t1_c + h) ? 3 : 4;
  } else if (current == 3) {
    if (inlet_c < cfg.t1_c - h)
      mapped = 4;
    else if (inlet_c >= cfg.t2_c + h)
      mapped = 2;
    else
      mapped = 3;
  } else if (current == 2) {
    if (inlet_c < cfg.t2_c - h)
      mapped = 3;
    else if (inlet_c >= cfg.t3_c + h)
      mapped = 1;
    else
      mapped = 2;
  } else {
    mapped = (inlet_c < cfg.t3_c - h) ? 2 : 1;
  }
  return clamp_int(mapped, 1, base_level < 1 ? 1 : base_level);
}

// ---------------------------------------------------------------------------
// Zachte aanloop: begint op niveau 1 en stapt per step_ms één niveau op.
// ---------------------------------------------------------------------------
struct SoftStart {
  int cap = 0;  // 0 = geen begrenzing
  uint32_t next_ms = 0;
};

inline int update_soft_start(SoftStart& s, bool hp_phase_active, uint32_t now_ms, uint32_t step_ms) {
  if (!hp_phase_active || step_ms == 0) {
    s = SoftStart{};
    return 0;
  }
  if (s.cap <= 0) {
    s.cap = 1;
    s.next_ms = now_ms + step_ms;
  } else if (s.cap < 10 && (int32_t)(now_ms - s.next_ms) >= 0) {
    s.cap += 1;
    s.next_ms = now_ms + step_ms;
  }
  return s.cap;
}

inline int apply_cap(int level, int cap) { return (cap > 0 && level > cap) ? cap : level; }

// ---------------------------------------------------------------------------
// Single-HP: lead kiezen en vastzetten voor de cyclus.
// ---------------------------------------------------------------------------
// `locked` is de vastgezette lead (0 = nog geen). Een harde storing breekt de
// lock; een frequentiebegrenzing bewust niet (dan zou hij gaan flipperen).
inline int choose_lead(int locked, bool hp1_fault, bool hp2_fault, bool hp1_limited, bool hp2_limited,
                       bool lead_is_hp1) {
  int lead = locked;
  if (lead == 1 && hp1_fault && !hp2_fault) lead = 0;
  if (lead == 2 && hp2_fault && !hp1_fault) lead = 0;
  if (lead == 0) {
    if (hp1_fault != hp2_fault)
      lead = hp1_fault ? 2 : 1;
    else if (hp1_limited != hp2_limited)
      lead = hp1_limited ? 2 : 1;
    else
      lead = lead_is_hp1 ? 1 : 2;
  }
  return lead;
}

// ---------------------------------------------------------------------------
// Stijgsnelheid van tank top over een venster van 2 minuten [K/min].
// ---------------------------------------------------------------------------
struct RiseTracker {
  float prev_c = NAN;
  uint32_t prev_ms = 0;
  float rise_k_min = NAN;
};

inline void reset_rise(RiseTracker& r) { r = RiseTracker{}; }

inline float update_rise(RiseTracker& r, uint32_t now_ms, float top_c) {
  if (isnan(top_c)) return r.rise_k_min;
  if (r.prev_ms == 0 || isnan(r.prev_c)) {
    r.prev_c = top_c;
    r.prev_ms = now_ms == 0 ? 1 : now_ms;
  } else if ((uint32_t)(now_ms - r.prev_ms) >= 120000UL) {
    const float dt_min = (uint32_t)(now_ms - r.prev_ms) / 60000.0f;
    r.rise_k_min = (top_c - r.prev_c) / dt_min;
    r.prev_c = top_c;
    r.prev_ms = now_ms;
  }
  return r.rise_k_min;
}

// ---------------------------------------------------------------------------
// Thermische bewaking (persgas en water-uit), gelatcht met release-marge.
// ---------------------------------------------------------------------------
struct GuardConfig {
  float discharge_max_c = 90.0f;
  float water_out_max_c = 57.0f;
};

inline bool above(float value, float limit) { return !isnan(value) && value >= limit; }

struct GuardInputs {
  float discharge_hp1_c = NAN;
  float discharge_hp2_c = NAN;
  float water_out_hp1_c = NAN;
  float water_out_hp2_c = NAN;
  bool assist_is_hp2 = true;
};

struct GuardState {
  bool assist_guard = false;  // tweede-HP-assist geblokkeerd
  bool boost_limit = false;   // snelboost: HP's eruit, element door
};

struct GuardEvent {
  bool trip_now = false;         // persgas of water-uit boven de grens
  bool water_only_trip = false;  // alleen de watergrens, geen persgas
};

inline GuardEvent update_guards(GuardState& g, const GuardInputs& in, const GuardConfig& cfg) {
  const float dis_max = cfg.discharge_max_c;
  const float wout_max = cfg.water_out_max_c;
  const float wout_assist = in.assist_is_hp2 ? in.water_out_hp2_c : in.water_out_hp1_c;
  const bool dis_over = above(in.discharge_hp1_c, dis_max) || above(in.discharge_hp2_c, dis_max);
  const bool wout_over = above(wout_assist, wout_max);
  const bool still_hot = above(in.discharge_hp1_c, dis_max - 5.0f) || above(in.discharge_hp2_c, dis_max - 5.0f) ||
                         above(wout_assist, wout_max - 2.0f);

  // Snelboost: beide units draaien, dus de heetste water-uit telt.
  const bool boost_over = dis_over || above(in.water_out_hp1_c, wout_max) || above(in.water_out_hp2_c, wout_max);
  const bool boost_still_hot = above(in.discharge_hp1_c, dis_max - 5.0f) || above(in.discharge_hp2_c, dis_max - 5.0f) ||
                               above(in.water_out_hp1_c, wout_max - 2.0f) || above(in.water_out_hp2_c, wout_max - 2.0f);
  if (boost_over) {
    g.boost_limit = true;
  } else if (g.boost_limit && !boost_still_hot) {
    g.boost_limit = false;
  }

  GuardEvent event;
  if (dis_over || wout_over) {
    g.assist_guard = true;
    event.trip_now = true;
    event.water_only_trip = wout_over && !dis_over;
  } else if (g.assist_guard && !still_hot) {
    g.assist_guard = false;
  }
  return event;
}

// ---------------------------------------------------------------------------
// Stapsgewijze tweede-HP-assist in single-HP-modus.
// ---------------------------------------------------------------------------
struct AssistConfig {
  int max_level = 3;
  float min_rise_k_min = 0.10f;
  float bottom_stop_c = 48.0f;
  float water_release_c = 50.0f;
  float water_out_max_c = 57.0f;
  uint32_t engage_ms = 600000UL;   // 10 min aanhoudend tekort
  uint32_t step_ms = 600000UL;     // 10 min per stap
  uint32_t min_on_ms = 900000UL;   // 15 min minimale draaitijd
  uint32_t lockout_ms = 900000UL;  // 15 min wachttijd na eruit gaan
};

struct AssistInputs {
  uint32_t now_ms = 0;
  uint32_t dt_ms = 0;
  bool context = false;  // assist aan, single-HP, HP-vraag, toestand DHW_HEAT_PUMP
  bool guard = false;
  GuardEvent trip{};
  float tank_bottom_c = NAN;
  float water_out_assist_c = NAN;
  bool saturated = false;  // lead krijgt zijn volle toegestane niveau
  float rise_k_min = NAN;
};

struct AssistState {
  int level = 0;
  uint32_t engage_ms = 0;
  uint32_t step_ms = 0;
  uint32_t on_since_ms = 0;
  uint32_t lockout_until_ms = 0;
  bool water_trip = false;
};

inline void drop_assist(AssistState& s, uint32_t now_ms, bool lockout, uint32_t lockout_ms) {
  s.level = 0;
  s.engage_ms = 0;
  s.step_ms = 0;
  s.on_since_ms = 0;
  if (lockout) {
    s.lockout_until_ms = (now_ms + lockout_ms) == 0 ? 1 : now_ms + lockout_ms;
  } else {
    s.lockout_until_ms = 0;
    s.water_trip = false;
  }
}

inline int update_assist(AssistState& s, const AssistInputs& in, const AssistConfig& cfg) {
  const bool tail = !isnan(in.tank_bottom_c) && in.tank_bottom_c >= cfg.bottom_stop_c;
  // Onthouden of de laatste trip puur de watergrens was: alleen dan mag hij
  // terug zodra het water is afgekoeld. Bij persgas blijft de wachttijd staan.
  if (in.trip.trip_now) s.water_trip = in.trip.water_only_trip;
  if (!in.context) {
    drop_assist(s, in.now_ms, false, cfg.lockout_ms);
    return s.level;
  }
  if (in.guard) {
    if (s.level > 0) drop_assist(s, in.now_ms, true, cfg.lockout_ms);
    return s.level;
  }
  if (tail) {
    if (s.level > 0) drop_assist(s, in.now_ms, false, cfg.lockout_ms);
    s.engage_ms = 0;
    return s.level;
  }
  const int cap = clamp_int(cfg.max_level, 1, 10);
  const bool too_slow = !isnan(in.rise_k_min) && in.rise_k_min < cfg.min_rise_k_min;
  const bool short_of_it = in.saturated && too_slow;

  if (s.level == 0) {
    const bool wait_elapsed = s.lockout_until_ms == 0 || (int32_t)(in.now_ms - s.lockout_until_ms) >= 0;
    float release_c = cfg.water_release_c;
    const float release_ceiling = cfg.water_out_max_c - 2.0f;
    if (!isnan(release_c) && release_c > release_ceiling) release_c = release_ceiling;
    const bool water_released =
        s.water_trip && !isnan(in.water_out_assist_c) && !isnan(release_c) && in.water_out_assist_c < release_c;
    const bool locked_out = !wait_elapsed && !water_released;
    if (short_of_it && !locked_out)
      s.engage_ms += in.dt_ms;
    else
      s.engage_ms = 0;
    if (s.engage_ms >= cfg.engage_ms) {
      s.level = 1;
      s.on_since_ms = in.now_ms;
      s.step_ms = 0;
      s.engage_ms = 0;
      s.lockout_until_ms = 0;
      s.water_trip = false;
    }
  } else {
    const uint32_t on_elapsed = (uint32_t)(in.now_ms - s.on_since_ms);
    s.step_ms += in.dt_ms;
    if (short_of_it) {
      if (s.level < cap && s.step_ms >= cfg.step_ms) {
        s.level += 1;
        s.step_ms = 0;
      }
    } else if (s.step_ms >= cfg.step_ms) {
      if (s.level > 1) {
        s.level -= 1;
        s.step_ms = 0;
      } else if (on_elapsed >= cfg.min_on_ms) {
        drop_assist(s, in.now_ms, true, cfg.lockout_ms);
      }
    }
  }
  return s.level;
}

// ---------------------------------------------------------------------------
// Niveaus per warmtepomp.
// ---------------------------------------------------------------------------
struct LevelInputs {
  bool duo = true;
  bool hp_request = false;
  int level = 0;  // na mapping en zachte aanloop
  bool single_mode = false;
  bool max_boost = false;
  int lead = 1;
  int bump = 0;
  int soft_start_cap = 0;
  int assist_level = 0;
};

struct Levels {
  int hp1 = 0;
  int hp2 = 0;
  int owner = 0;
  const char* reason = "dhw_idle";
};

inline Levels compute_levels(const LevelInputs& in) {
  Levels out;
  const int level = clamp_int(in.level, 0, 10);
  if (!in.hp_request || level <= 0) return out;
  if (!in.duo) {
    out.hp1 = level;
    out.owner = 1;
    out.reason = "dhw_single_hp1";
    return out;
  }
  // Snelboost vraagt juist capaciteit: altijd beide units.
  if (in.single_mode && !in.max_boost) {
    const int boosted = apply_cap(clamp_int(level + clamp_int(in.bump, 0, 3), 1, 10), in.soft_start_cap);
    const int assist = clamp_int(in.assist_level, 0, 10);
    if (in.lead == 2) {
      out.hp1 = assist;
      out.hp2 = boosted;
      out.owner = assist > 0 ? 3 : 2;
      out.reason = assist > 0 ? "dhw_single_hp2_assist" : "dhw_single_hp2";
    } else {
      out.hp1 = boosted;
      out.hp2 = assist;
      out.owner = assist > 0 ? 3 : 1;
      out.reason = assist > 0 ? "dhw_single_hp1_assist" : "dhw_single_hp1";
    }
    return out;
  }
  out.hp1 = level;
  out.hp2 = level;
  out.owner = 3;
  out.reason = in.max_boost ? "dhw_max_boost" : "dhw_duo";
  return out;
}

}  // namespace oq_fe_dhw_levels
