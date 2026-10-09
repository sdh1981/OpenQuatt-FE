// Host-test voor het FE-tankmodel: tapdetectie, standby-loss-lerer, ETA's en
// de element-only-thermostaat (port van de LilyGO-build).
//
//   c++ -std=c++17 -Wall -Wextra -Werror tests/host/fe_dhw_tank_test.cpp -o /tmp/t && /tmp/t

#undef NDEBUG
#include <assert.h>
#include <math.h>
#include <stdint.h>

#include "../../openquatt/includes/fe/oq_fe_dhw_tank_logic.h"

namespace {

using namespace oq_fe_dhw_tank;

bool near(float a, float b, float eps) { return fabsf(a - b) < eps; }

// Een douche: tank top zakt 1 K/min, daarna twee minuten rust.
void test_tap_detected_and_closed() {
  const TapConfig cfg;  // 0,4 K/min, 220 L
  TapState s;
  uint32_t t = 1000U;
  update_tap(s, true, false, 50.0f, t, cfg);  // eerste meting
  t += 60000U;
  update_tap(s, true, false, 49.0f, t, cfg);
  assert(s.active && s.taps_today == 1 && s.seen);
  t += 60000U;
  update_tap(s, true, false, 48.0f, t, cfg);
  assert(s.active && s.taps_today == 1);  // zelfde tapping
  t += 60000U;
  update_tap(s, true, false, 48.0f, t, cfg);
  assert(s.active);  // pas 1 min rust
  t += 60000U;
  update_tap(s, true, false, 48.0f, t, cfg);
  assert(!s.active);
  assert(near(s.last_tap_wh, 2.0f * 220.0f * 1.16f, 0.5f));  // 50 -> 48
}

// Standby-afkoeling is geen tapping; tijdens verwarmen wordt niet gemeten.
void test_tap_not_on_standby_or_heating() {
  const TapConfig cfg;
  TapState s;
  update_tap(s, true, false, 50.0f, 0U, cfg);
  update_tap(s, true, false, 49.98f, 60000U, cfg);
  assert(!s.active);
  update_tap(s, true, true, 40.0f, 120000U, cfg);  // verwarmen
  assert(!s.active && isnan(s.prev_c));
  update_tap(s, false, false, 40.0f, 180000U, cfg);  // uitgeschakeld
  assert(!s.active);
}

// Een half uur rust, 0,5 K afkoeling op 55 C, ruimte 20 C:
// 0,5 * 255,2 Wh/K / 0,5 h / 35 K = 7,29 W/K.
void test_learner_sample() {
  const LearnerConfig cfg;
  LearnerState s;
  const TapState tap;  // nooit getapt
  float ua = 0.0f;
  int n = 0;
  assert(!update_learner(s, ua, n, true, 55.0f, 1000U, tap, cfg));
  assert(update_learner(s, ua, n, true, 54.5f, 1000U + 1800000U, tap, cfg));
  assert(n == 1 && near(ua, 0.5f * 255.2f / 0.5f / 35.0f, 0.01f));
}

void test_learner_rejects_and_excludes() {
  const LearnerConfig cfg;
  float ua = 0.0f;
  int n = 0;
  // Te snelle daling (> 5 K): weg.
  LearnerState s;
  const TapState tap;
  update_learner(s, ua, n, true, 55.0f, 0U, tap, cfg);
  assert(!update_learner(s, ua, n, true, 49.0f, 1800000U, tap, cfg));
  assert(n == 0);
  // Tank te dicht bij de ruimtetemperatuur: weg.
  LearnerConfig warm = cfg;
  warm.ambient_c = 52.0f;
  LearnerState s2;
  update_learner(s2, ua, n, true, 55.0f, 0U, tap, warm);
  assert(!update_learner(s2, ua, n, true, 54.5f, 1800000U, tap, warm));
  // Binnen een uur na een tapping wordt niet gemeten.
  TapState tapped;
  tapped.seen = true;
  tapped.last_tap_ms = 0U;
  LearnerState s3;
  update_learner(s3, ua, n, true, 55.0f, 10U, tapped, cfg);
  assert(isnan(s3.start_c));
  // Niet in rust: venster weg.
  LearnerState s4;
  update_learner(s4, ua, n, true, 55.0f, 0U, tap, cfg);
  update_learner(s4, ua, n, false, 55.0f, 1000U, tap, cfg);
  assert(isnan(s4.start_c));
}

// Na vier monsters gaat de middeling over op een trage EMA.
void test_learner_averaging() {
  LearnerConfig cfg;
  float ua = 0.0f;
  int n = 0;
  const TapState tap;
  LearnerState s;
  uint32_t t = 0;
  update_learner(s, ua, n, true, 55.0f, t, tap, cfg);
  for (int i = 0; i < 4; ++i) {
    t += 1800000U;
    update_learner(s, ua, n, true, 55.0f - 0.5f * (i + 1), t, tap, cfg);
  }
  assert(n == 4);
  const float before = ua;
  t += 1800000U;
  update_learner(s, ua, n, true, 53.0f - 0.9f, t, tap, cfg);  // groter monster (13,9 W/K), nog plausibel
  assert(n == 5 && ua > before && ua < before + 0.2f);        // EMA beweegt maar een beetje
}

void test_time_to_ready() {
  // 10 K tekort, 220 L, 6 kW HP, geen verlies: 10*255,2/6000*60 = 25,5 min.
  assert(near(time_to_ready_min(true, 39.0f, 49.0f, 220.0f, 6000.0f, false, 0.0f, 20.0f), 25.52f, 0.05f));
  assert(time_to_ready_min(true, 49.0f, 49.0f, 220.0f, 6000.0f, false, 0.0f, 20.0f) == 0.0f);
  assert(isnan(time_to_ready_min(false, 39.0f, 49.0f, 220.0f, 6000.0f, false, 0.0f, 20.0f)));
  assert(isnan(time_to_ready_min(true, 39.0f, 49.0f, 220.0f, 0.0f, false, 0.0f, 20.0f)));
}

void test_legionella_eta() {
  LegionellaEtaInputs in;
  in.top_c = 60.0f;  // boven het HP-plafond: alleen element en hold
  const float eta = legionella_eta_min(in);
  // (68-60)*255,2/3000*60 = 40,8 min + 15 min hold.
  assert(near(eta, 55.8f, 0.1f));
  in.hold_started = true;
  in.hold_elapsed_ms = 5U * 60000U;
  in.top_c = 68.0f;
  assert(near(legionella_eta_min(in), 10.0f, 0.01f));
}

void test_element_only_thermostat() {
  // Doel 50, off-delta 5.
  assert(element_only_on(false, true, 48.0f, 50.0f, 5.0f));    // bij binnenkomst onder doel: aan
  assert(!element_only_on(true, false, 50.0f, 50.0f, 5.0f));   // doel bereikt: uit
  assert(!element_only_on(false, false, 46.0f, 50.0f, 5.0f));  // in de hysterese: blijft uit
  assert(element_only_on(true, false, 46.0f, 50.0f, 5.0f));    // in de hysterese: blijft aan
  assert(element_only_on(false, false, 44.9f, 50.0f, 5.0f));   // onder 45: aan
  assert(!element_only_on(true, false, NAN, 50.0f, 5.0f));     // sensor weg: uit
}

}  // namespace

int main() {
  test_tap_detected_and_closed();
  test_tap_not_on_standby_or_heating();
  test_learner_sample();
  test_learner_rejects_and_excludes();
  test_learner_averaging();
  test_time_to_ready();
  test_legionella_eta();
  test_element_only_thermostat();
  return 0;
}
