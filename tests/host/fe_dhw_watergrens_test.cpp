// Host-test voor de watergrens tijdens warm water (fe.16).
//
// Aanleiding: met "Maximum water temperature" op 45 C (vloerverwarming) ligt
// upstream's water-noodstop op 50 C. Tijdens legionella kwam de aanvoer op
// 51,6 C; de DHW-regeling zag de noodstop als HP_FAULT en bleef in storing.
// Nu: zolang de klep bevestigd op DHW staat geldt het DHW-maximum, en een
// noodstop beëindigt alleen de HP-fase.
//
//   c++ -std=c++17 -Wall -Wextra -Werror tests/host/fe_dhw_watergrens_test.cpp -o /tmp/t && /tmp/t

#undef NDEBUG
#include <assert.h>
#include <math.h>
#include <stdint.h>

#include "../../openquatt/includes/fe/oq_dhw_controller_logic.h"
#include "../../openquatt/includes/fe/oq_fe_dhw_bridge.h"
#include "../../openquatt/includes/fe/oq_fe_dhw_settings_record.h"

namespace {

using oq_dhw::Config;
using oq_dhw::Controller;
using oq_dhw::Fault;
using oq_dhw::Inputs;
using oq_dhw::State;

Inputs make_inputs(uint32_t now_ms, float top_c, float bottom_c) {
  Inputs in;
  in.now_ms = now_ms;
  in.tank_top_c = top_c;
  in.tank_bottom_c = bottom_c;
  in.valve_feedback_valid = true;
  in.valve_feedback_cv = false;  // klep op DHW
  in.flow_valid = false;
  in.legionella_seeded = true;
  return in;
}

uint32_t run_to(Controller& c, const Config& cfg, uint32_t now_ms, Inputs base, State target) {
  for (int i = 0; i < 20; ++i) {
    base.now_ms = now_ms;
    const auto out = c.tick(base, cfg);
    if (out.state == target) return now_ms;
    now_ms += 2000U;
  }
  assert(false && "doeltoestand niet bereikt");
  return now_ms;
}

// Noodstop in de HP-fase: door naar het element (boost na HP), geen storing.
void test_trip_in_heat_pump_goes_to_boost() {
  Config cfg;
  Controller c;
  uint32_t now = run_to(c, cfg, 1000U, make_inputs(0, 40.0f, 30.0f), State::DHW_HEAT_PUMP);
  now += 2000U;
  Inputs in = make_inputs(now, 42.0f, 35.0f);
  in.water_trip_active = true;
  const auto out = c.tick(in, cfg);
  assert(out.state == State::DHW_BOOST);
  assert(out.fault == Fault::NONE);
  assert(out.element_on);
  assert(!out.hp_dhw_request);
}

// Zonder boost na HP: de cyclus eindigt gewoon, geen storing.
void test_trip_in_heat_pump_without_boost_ends_cycle() {
  Config cfg;
  cfg.enable_boost_after_hp = false;
  Controller c;
  uint32_t now = run_to(c, cfg, 1000U, make_inputs(0, 40.0f, 30.0f), State::DHW_HEAT_PUMP);
  now += 2000U;
  Inputs in = make_inputs(now, 42.0f, 35.0f);
  in.water_trip_active = true;
  const auto out = c.tick(in, cfg);
  assert(out.state == State::IDLE_CV);
  assert(out.fault == Fault::NONE);
}

// Legionella: de noodstop sluit de HP-fase af; het element maakt het af.
void test_trip_in_legionella_hands_over_to_element() {
  Config cfg;
  Controller c;
  Inputs start = make_inputs(0, 50.0f, 45.0f);
  start.legionella_force_request = true;
  uint32_t now = run_to(c, cfg, 1000U, start, State::LEGIONELLA);
  now += 2000U;
  Inputs in = make_inputs(now, 51.0f, 48.0f);
  in.water_trip_active = true;
  c.tick(in, cfg);
  now += 2000U;
  in.now_ms = now;
  const auto out = c.tick(in, cfg);
  assert(out.state == State::LEGIONELLA);
  assert(out.fault == Fault::NONE);
  assert(!out.hp_dhw_request);
  assert(out.element_on);
}

// In rust doet de noodstop niets met de DHW-regeling.
void test_trip_at_rest_is_no_fault() {
  Config cfg;
  Controller c;
  Inputs in = make_inputs(1000U, 50.0f, 50.0f);
  in.water_trip_active = true;
  const auto out = c.tick(in, cfg);
  assert(out.state == State::IDLE_CV);
  assert(out.fault == Fault::NONE);
}

// Echte warmtepompstoringen geven nog steeds HP_FAULT.
void test_real_hp_fault_still_latches() {
  Config cfg;
  Controller c;
  uint32_t now = run_to(c, cfg, 1000U, make_inputs(0, 40.0f, 30.0f), State::DHW_HEAT_PUMP);
  now += 2000U;
  Inputs in = make_inputs(now, 42.0f, 35.0f);
  in.hp_fault_active = true;
  const auto out = c.tick(in, cfg);
  assert(out.state == State::FAULT);
  assert(out.fault == Fault::HP_FAULT);
}

// De grens: DHW-maximum alleen als DHW de aanvoer heeft en het maximum geldig is.
void test_water_limit_selection() {
  using oq_fe_dhw_bridge::water_limit_max_c;
  assert(water_limit_max_c(false, 65.0f, 45.0f) == 45.0f);
  assert(water_limit_max_c(true, 65.0f, 45.0f) == 65.0f);
  assert(water_limit_max_c(true, NAN, 45.0f) == 45.0f);
}

// Na een wissel van het geldende maximum telt de noodstop pas na de settle-tijd.
void test_water_limit_settled() {
  using oq_fe_dhw_bridge::water_limit_settled;
  assert(water_limit_settled(0, 5000U, 12000U));
  assert(!water_limit_settled(1000U, 12999U, 12000U));
  assert(water_limit_settled(1000U, 13000U, 12000U));
  assert(water_limit_settled(0xFFFFF000U, 0x00002000U, 12000U));  // millis-overloop
}

// Het record bewaart het maximum in de vroegere reservebyte; 0 = niet gezet.
void test_record_byte() {
  using namespace oq_fe_dhw_settings;
  float value = 0.0f;
  assert(encode_whole_c(65.0f) == 65);
  assert(encode_whole_c(64.6f) == 65);
  assert(encode_whole_c(NAN) == 0);
  assert(encode_whole_c(0.0f) == 0);
  assert(!decode_whole_c(0, value));
  assert(decode_whole_c(65, value) && value == 65.0f);

  // Een record van vóór fe.16 (byte 0) blijft geldig.
  Record old_record = empty_record();
  seal(old_record);
  assert(is_valid(old_record));
  assert(old_record.dhw_max_water_c == 0);

  Record a = empty_record();
  Record b = empty_record();
  b.dhw_max_water_c = 60;
  assert(!same_values(a, b));
  seal(b);
  assert(is_valid(b) && b.dhw_max_water_c == 60);
}

}  // namespace

int main() {
  test_trip_in_heat_pump_goes_to_boost();
  test_trip_in_heat_pump_without_boost_ends_cycle();
  test_trip_in_legionella_hands_over_to_element();
  test_trip_at_rest_is_no_fault();
  test_real_hp_fault_still_latches();
  test_water_limit_selection();
  test_water_limit_settled();
  test_record_byte();
  return 0;
}
