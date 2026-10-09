// Host-test voor de DHW-compressorniveaus van FE (port van de LilyGO-build).
//
//   c++ -std=c++17 -Wall -Wextra -Werror tests/host/fe_dhw_levels_test.cpp -o /tmp/t && /tmp/t

#undef NDEBUG
#include <assert.h>
#include <math.h>
#include <stdint.h>

#define OQ_FE_TARGET 1
#include "../../openquatt/includes/control/oq_thermal_request_logic.h"
#include "../../openquatt/includes/fe/oq_fe_dhw_bridge.h"
#include "../../openquatt/includes/fe/oq_fe_dhw_levels_logic.h"

namespace {

using namespace oq_fe_dhw_levels;

// Mapping zakt per drempel één niveau, met hysterese, en klimt pas terug onder
// de drempel min de hysterese.
void test_map_level_steps_down_and_up() {
  const MapConfig cfg;  // 40 / 44 / 48, hysterese 0,5
  int lvl = 4;
  lvl = map_level(lvl, 40.4f, cfg, 4);
  assert(lvl == 4);
  lvl = map_level(lvl, 40.5f, cfg, 4);
  assert(lvl == 3);
  lvl = map_level(lvl, 44.5f, cfg, 4);
  assert(lvl == 2);
  lvl = map_level(lvl, 48.5f, cfg, 4);
  assert(lvl == 1);
  lvl = map_level(lvl, 47.6f, cfg, 4);  // binnen de hysterese: blijft 1
  assert(lvl == 1);
  lvl = map_level(lvl, 47.4f, cfg, 4);
  assert(lvl == 2);
}

void test_map_level_bounds() {
  const MapConfig cfg;
  assert(map_level(4, 20.0f, cfg, 3) == 3);  // nooit boven het basisniveau
  assert(map_level(1, 60.0f, cfg, 4) == 1);  // nooit uit
  assert(map_level(2, NAN, cfg, 4) == 4);    // geen meting: basisniveau
}

void test_soft_start() {
  SoftStart s;
  assert(update_soft_start(s, true, 1000U, 180000U) == 1);
  assert(update_soft_start(s, true, 180999U, 180000U) == 1);
  assert(update_soft_start(s, true, 181000U, 180000U) == 2);
  assert(update_soft_start(s, false, 200000U, 180000U) == 0);  // HP-fase voorbij: reset
  assert(update_soft_start(s, true, 300000U, 0U) == 0);        // uitgeschakeld
  assert(apply_cap(4, 2) == 2 && apply_cap(4, 0) == 4);
}

void test_choose_lead() {
  assert(choose_lead(0, false, false, false, false, true) == 1);
  assert(choose_lead(0, false, false, false, false, false) == 2);
  assert(choose_lead(0, true, false, false, false, true) == 2);  // HP1 in storing
  assert(choose_lead(0, false, false, true, false, true) == 2);  // HP1 begrensd
  assert(choose_lead(1, false, false, true, false, true) == 1);  // begrenzing breekt de lock niet
  assert(choose_lead(1, true, false, false, false, true) == 2);  // storing wel
}

void test_compute_levels() {
  LevelInputs in;
  in.hp_request = true;
  in.level = 4;
  auto l = compute_levels(in);
  assert(l.hp1 == 4 && l.hp2 == 4 && l.owner == 3);

  in.single_mode = true;
  in.lead = 2;
  in.bump = 1;
  l = compute_levels(in);
  assert(l.hp1 == 0 && l.hp2 == 5 && l.owner == 2);

  in.soft_start_cap = 2;  // de aanloop begrenst ook het opgehoogde niveau
  in.assist_level = 1;
  l = compute_levels(in);
  assert(l.hp1 == 1 && l.hp2 == 2 && l.owner == 3);

  in.max_boost = true;  // snelboost: altijd beide units
  l = compute_levels(in);
  assert(l.hp1 == 4 && l.hp2 == 4);

  in.hp_request = false;
  l = compute_levels(in);
  assert(l.hp1 == 0 && l.hp2 == 0 && l.owner == 0);
}

void test_rise_tracker() {
  RiseTracker r;
  assert(isnan(update_rise(r, 1000U, 40.0f)));
  assert(isnan(update_rise(r, 60000U, 40.5f)));  // nog geen 2 minuten
  const float rise = update_rise(r, 121000U, 41.0f);
  assert(fabsf(rise - 0.5f) < 0.001f);
}

// De assist komt pas na 10 minuten tekort, stapt op, en gaat er bij de
// staart van de cyclus meteen uit.
void test_assist_cycle() {
  const AssistConfig cfg;
  AssistState s;
  AssistInputs in;
  in.context = true;
  in.saturated = true;
  in.rise_k_min = 0.05f;
  in.tank_bottom_c = 30.0f;
  in.dt_ms = 60000U;
  for (int i = 1; i <= 9; ++i) {
    in.now_ms = i * 60000U;
    assert(update_assist(s, in, cfg) == 0);
  }
  in.now_ms = 10U * 60000U;
  assert(update_assist(s, in, cfg) == 1);
  for (int i = 11; i <= 20; ++i) {
    in.now_ms = i * 60000U;
    update_assist(s, in, cfg);
  }
  assert(s.level == 2);
  in.tank_bottom_c = 48.0f;  // staart: meteen eruit, zonder wachttijd
  assert(update_assist(s, in, cfg) == 0);
  assert(s.lockout_until_ms == 0);
}

// Thermische grens: eruit met wachttijd; na een pure watertrip mag hij terug
// zodra het water onder de vrijgave zakt.
void test_assist_guard_and_water_release() {
  const AssistConfig cfg;
  AssistState s;
  s.level = 2;
  s.on_since_ms = 1;
  AssistInputs in;
  in.context = true;
  in.saturated = true;
  in.rise_k_min = 0.05f;
  in.tank_bottom_c = 30.0f;
  in.dt_ms = 60000U;
  in.now_ms = 100000U;
  in.guard = true;
  in.trip = GuardEvent{true, true};
  assert(update_assist(s, in, cfg) == 0);
  assert(s.lockout_until_ms != 0 && s.water_trip);

  in.guard = false;
  in.trip = GuardEvent{};
  in.water_out_assist_c = 49.0f;  // onder de vrijgave van 50
  for (int i = 0; i < 10; ++i) {
    in.now_ms += 60000U;
    update_assist(s, in, cfg);
  }
  assert(s.level == 1);  // terug ondanks dat de wachttijd nog liep
}

void test_guards_latch_with_release_margin() {
  const GuardConfig cfg;  // persgas 90, water-uit 57
  GuardState g;
  GuardInputs in;
  in.water_out_hp2_c = 57.0f;
  auto ev = update_guards(g, in, cfg);
  assert(g.assist_guard && g.boost_limit && ev.trip_now && ev.water_only_trip);
  in.water_out_hp2_c = 55.5f;  // binnen de release-marge van 2 K
  update_guards(g, in, cfg);
  assert(g.assist_guard && g.boost_limit);
  in.water_out_hp2_c = 54.9f;
  update_guards(g, in, cfg);
  assert(!g.assist_guard && !g.boost_limit);

  in.discharge_hp1_c = 90.0f;  // persgas: geen pure watertrip
  ev = update_guards(g, in, cfg);
  assert(ev.trip_now && !ev.water_only_trip);
  // Snelboost kijkt ook naar de water-uit van HP1; de assist niet (die is HP2).
  GuardState g2;
  GuardInputs in2;
  in2.water_out_hp1_c = 58.0f;
  update_guards(g2, in2, cfg);
  assert(g2.boost_limit && !g2.assist_guard);
}

void test_bridge_base_target() {
  using oq_fe_dhw_bridge::base_target;
  assert(base_target(5, true, false, false, false) == oq_fe_dhw_bridge::CM_DHW);  // DHW gaat voor koelen
  assert(base_target(2, true, false, false, false) == 10);
  assert(base_target(2, true, false, true, false) == 1);  // flow-interlock houdt CM1
  assert(base_target(5, false, false, false, false) == 5);
  // Element-only gaat voor koelen en verwarmen, maar DHW en vorst gaan voor.
  assert(base_target(2, false, true, false, false) == oq_fe_dhw_bridge::CM_ELEMENT_ONLY);
  assert(base_target(5, false, true, false, false) == 11);
  assert(base_target(98, false, true, false, true) == 98);
  assert(base_target(2, true, true, false, false) == 10);  // legionella of lopende cyclus
  assert(oq_fe_dhw_bridge::needs_postflow_before_element_only(10));
  assert(!oq_fe_dhw_bridge::needs_postflow_before_element_only(0));
  assert(!oq_fe_dhw_bridge::needs_postflow_before_element_only(1));
}

// De upstream-haak: CM10 laat de HP's in verwarmstand draaien op de DHW-niveaus.
void test_thermal_request_hook() {
  const auto mode = oq_request::resolve_mode_context(oq_fe_dhw_bridge::CM_DHW, 3);
  assert(mode.cm_allows_hp && !mode.cooling && !mode.power_house && !mode.curve);
  assert(mode.thermal_mode_code == 2 && mode.strategy_code == oq_fe_dhw_bridge::STRATEGY_DHW);

  auto& bridge = oq_fe_dhw_bridge::state();
  bridge.hp1_level = 3;
  bridge.hp2_level = 2;
  bridge.owner_hp = 3;
  bridge.reason = "dhw_duo";
  oq_request::StrategyRequestInput in{};
  in.mode = mode;
  in.duo = true;
  auto req = oq_request::select_strategy_request(in);
  assert(req.hp1_level == 3 && req.hp2_level == 2 && req.owner_hp == 3);
  in.duo = false;
  req = oq_request::select_strategy_request(in);
  assert(req.hp1_level == 3 && req.hp2_level == 0);
  assert(oq_request::sanitize_request_strategy_code(oq_fe_dhw_bridge::STRATEGY_DHW) == 5);
  oq_fe_dhw_bridge::clear();

  // Andere modi blijven upstream.
  assert(oq_request::resolve_mode_context(2, 3).strategy_code == oq_request::STRATEGY_POWER_HOUSE);
  assert(!oq_request::resolve_mode_context(0, 3).cm_allows_hp);
}

}  // namespace

int main() {
  test_thermal_request_hook();
  test_map_level_steps_down_and_up();
  test_map_level_bounds();
  test_soft_start();
  test_choose_lead();
  test_compute_levels();
  test_rise_tracker();
  test_assist_cycle();
  test_assist_guard_and_water_release();
  test_guards_latch_with_release_margin();
  test_bridge_base_target();
  return 0;
}
