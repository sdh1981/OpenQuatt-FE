// OpenQuatt FE - tankmodel: tapdetectie, standby-loss-lerer, ETA's en element-only.
//
// Port van de LilyGO-build (oq_boiler_control.yaml en oq_supervisory_controlmode.yaml,
// v0.65.0) naar pure logica. Afwijking: de ruimtetemperatuur rond de tank is een
// invoer (instelbaar of uit Home Assistant) in plaats van vast 20 C.
#pragma once

#include <math.h>
#include <stdint.h>

namespace oq_fe_dhw_tank {

inline float wh_per_k(float volume_l) { return (volume_l > 0.0f ? volume_l : 220.0f) * 1.16f; }

// ---------------------------------------------------------------------------
// Tapdetectie: daalsnelheid van tank top terwijl er niet verwarmd wordt.
// ---------------------------------------------------------------------------
struct TapConfig {
  float threshold_k_min = 0.4f;
  uint32_t window_ms = 60000UL;
  uint32_t calm_ms = 120000UL;  // pas na 2 min rust is de tapping voorbij
  float volume_l = 220.0f;
};

struct TapState {
  bool active = false;
  float prev_c = NAN;
  uint32_t prev_ms = 0;
  float rate_k_min = NAN;
  float start_c = NAN;
  uint32_t calm_ms = 0;
  bool seen = false;  // ooit een tapping gezien sinds de start
  uint32_t last_tap_ms = 0;
  int taps_today = 0;
  float last_tap_wh = NAN;
};

inline void update_tap(TapState& s, bool enabled, bool heating, float top_c, uint32_t now_ms, const TapConfig& cfg) {
  if (!enabled || heating || isnan(top_c)) {
    // Tijdens verwarmen zegt de tanktop niets over tappen.
    s.active = false;
    s.calm_ms = 0;
    s.prev_c = NAN;
    s.rate_k_min = NAN;
    return;
  }
  if (isnan(s.prev_c)) {
    s.prev_c = top_c;
    s.prev_ms = now_ms;
    return;
  }
  const uint32_t win_ms = now_ms - s.prev_ms;
  if (win_ms < cfg.window_ms) return;
  const float prev_c = s.prev_c;
  const float rate = (top_c - prev_c) / (win_ms / 60000.0f);
  s.rate_k_min = rate;
  s.prev_c = top_c;
  s.prev_ms = now_ms;

  if (rate <= -cfg.threshold_k_min) {
    if (!s.active) {
      s.active = true;
      s.start_c = prev_c;  // de meting van vóór de daling
      s.taps_today += 1;
    }
    s.calm_ms = 0;
    s.seen = true;
    s.last_tap_ms = now_ms;
  } else if (s.active) {
    s.calm_ms += win_ms;
    if (s.calm_ms >= cfg.calm_ms) {
      s.active = false;
      s.calm_ms = 0;
      // Ondergrens: stratificatie verbergt een deel van de onttrekking.
      if (!isnan(s.start_c) && s.start_c > top_c) s.last_tap_wh = (s.start_c - top_c) * wh_per_k(cfg.volume_l);
      s.start_c = NAN;
    }
  }
}

// ---------------------------------------------------------------------------
// Standby-loss-lerer: UA van de tank [W/K] uit de afkoeling in rust.
// ---------------------------------------------------------------------------
struct LearnerConfig {
  float volume_l = 220.0f;
  float ambient_c = 20.0f;
  uint32_t window_ms = 1800000UL;     // 30 min per monster
  uint32_t tap_quiet_ms = 3600000UL;  // 1 uur na de laatste tapping
  float tau_s = 7.0f * 86400.0f;      // EMA over ~7 dagen
};

struct LearnerState {
  float start_c = NAN;
  uint32_t start_ms = 0;
};

// Geeft true als er een monster is verwerkt. `ua` en `samples` worden bewaard.
inline bool update_learner(LearnerState& s, float& ua, int& samples, bool idle, float top_c, uint32_t now_ms,
                           const TapState& tap, const LearnerConfig& cfg) {
  if (!idle || isnan(top_c)) {
    s = LearnerState{};
    return false;
  }
  const bool quiet = !tap.seen || (uint32_t)(now_ms - tap.last_tap_ms) > cfg.tap_quiet_ms;
  if (!quiet) {
    s = LearnerState{};
    return false;
  }
  if (isnan(s.start_c)) {
    s.start_c = top_c;
    s.start_ms = now_ms;
    return false;
  }
  const uint32_t window_ms = now_ms - s.start_ms;
  if (window_ms < cfg.window_ms) return false;

  bool accepted = false;
  const float d_t = s.start_c - top_c;
  if (d_t > 0.05f && d_t < 5.0f) {
    const float hours = window_ms / 3600000.0f;
    const float d_ambient = s.start_c - cfg.ambient_c;
    if (d_ambient > 5.0f) {
      const float sample = (d_t * wh_per_k(cfg.volume_l) / hours) / d_ambient;
      if (sample > 0.2f && sample < 15.0f) {
        if (samples < 4 || ua <= 0.0f) {
          ua = (ua <= 0.0f) ? sample : ua + (sample - ua) / (float)(samples + 1);
        } else {
          const float a = 1.0f - expf(-(window_ms / 1000.0f) / cfg.tau_s);
          ua += a * (sample - ua);
        }
        samples += 1;
        accepted = true;
      }
    }
  }
  s.start_c = top_c;  // direct een nieuw venster
  s.start_ms = now_ms;
  return accepted;
}

// ---------------------------------------------------------------------------
// ETA's.
// ---------------------------------------------------------------------------
constexpr float ELEMENT_W = 3000.0f;

inline float net_power_w(float power_in_w, float ua, float top_c, float ambient_c) {
  if (ua > 0.0f) power_in_w -= ua * fmaxf(0.0f, top_c - ambient_c);
  return power_in_w;
}

// Minuten tot tank top het HP-stopdoel haalt; NAN buiten verwarmen.
inline float time_to_ready_min(bool heating, float top_c, float target_c, float volume_l, float hp_power_w,
                               bool element_on, float ua, float ambient_c) {
  if (!heating || isnan(top_c) || isnan(target_c)) return NAN;
  const float gap = target_c - top_c;
  if (gap <= 0.05f) return 0.0f;
  float p_in = isnan(hp_power_w) ? 0.0f : fmaxf(0.0f, hp_power_w);
  if (element_on) p_in += ELEMENT_W;
  const float p_net = net_power_w(p_in, ua, top_c, ambient_c);
  if (p_net <= 50.0f) return NAN;
  return gap * wh_per_k(volume_l) / p_net * 60.0f;
}

struct LegionellaEtaInputs {
  float top_c = NAN;
  float target_c = 68.0f;
  float hp_top_ceiling_c = 55.0f;
  float volume_l = 220.0f;
  bool hp_phase_active = false;
  float hp_power_w = NAN;
  bool hold_started = false;
  uint32_t hold_elapsed_ms = 0;
  uint32_t hold_ms = 15UL * 60UL * 1000UL;
  float ua = 0.0f;
  float ambient_c = 20.0f;
};

// Minuten tot een lopende legionella-run klaar is: HP-fase tot het plafond,
// element tot het doel, plus de hold.
inline float legionella_eta_min(const LegionellaEtaInputs& in) {
  if (isnan(in.top_c)) return NAN;
  const float whk = wh_per_k(in.volume_l);
  float total = 0.0f;
  if (in.hold_started) {
    total += in.hold_elapsed_ms < in.hold_ms ? (in.hold_ms - in.hold_elapsed_ms) / 60000.0f : 0.0f;
  } else {
    total += in.hold_ms / 60000.0f;
  }
  const float phase2_from = fmaxf(in.top_c, in.hp_top_ceiling_c);
  if (phase2_from < in.target_c) {
    const float p_net = net_power_w(ELEMENT_W, in.ua, phase2_from, in.ambient_c);
    if (p_net <= 50.0f) return NAN;  // het element komt niet boven het verlies uit
    total += (in.target_c - phase2_from) * whk / p_net * 60.0f;
  }
  if (in.hp_phase_active && in.top_c < in.hp_top_ceiling_c) {
    float p_hp = in.hp_power_w;
    if (isnan(p_hp) || p_hp <= 0.0f) p_hp = 3000.0f;  // voorzichtige aanname
    const float p_net = net_power_w(p_hp + ELEMENT_W, in.ua, in.top_c, in.ambient_c);
    if (p_net > 50.0f) total += (in.hp_top_ceiling_c - in.top_c) * whk / p_net * 60.0f;
  }
  return total;
}

// ---------------------------------------------------------------------------
// Element-only (CM11): thermostaat op tank bottom.
// ---------------------------------------------------------------------------
inline bool element_only_on(bool currently_on, bool just_entered, float bottom_c, float target_c, float off_delta_c) {
  if (isnan(bottom_c)) return false;  // sensor weg: element uit
  if (just_entered) return bottom_c < target_c;
  if (bottom_c >= target_c) return false;
  if (bottom_c < target_c - off_delta_c) return true;
  return currently_on;  // binnen de hysterese
}

}  // namespace oq_fe_dhw_tank
