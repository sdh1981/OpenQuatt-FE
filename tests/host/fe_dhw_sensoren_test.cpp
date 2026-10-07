// Host-test voor de sensorset van OpenQuatt FE.
//
// Op de Heatpump Controller Q zijn er alleen twee tanksensoren (DS18B20 voor
// top en bottom op de T-klem); de coil-sensoren uit de LilyGO-build vervallen.
// Deze test legt vast dat de DHW-toestandsmachine daarmee werkt, en dat tank
// top de verplichte sensor blijft.
//
//   c++ -std=c++17 -Wall -Wextra -Werror tests/host/fe_dhw_sensoren_test.cpp -o /tmp/t && /tmp/t

#undef NDEBUG
#include <assert.h>
#include <math.h>
#include <stdint.h>

#include "../../openquatt/includes/fe/oq_dhw_controller_logic.h"

namespace {

using oq_dhw::Config;
using oq_dhw::Controller;
using oq_dhw::Fault;
using oq_dhw::Inputs;
using oq_dhw::Outputs;
using oq_dhw::State;

// FE-sensorset: top en bottom, geen coil-sensoren. Klep meldt de DHW-stand.
Inputs fe_inputs(uint32_t now_ms, float top_c, float bottom_c) {
  Inputs in;
  in.now_ms = now_ms;
  in.tank_top_c = top_c;
  in.tank_bottom_c = bottom_c;
  in.coil_in_c = NAN;
  in.coil_out_c = NAN;
  in.valve_feedback_valid = true;
  in.valve_feedback_cv = false;
  return in;
}

void assert_fail_safe(const Outputs& out) {
  assert(out.state == State::FAULT);
  assert(out.fault == Fault::SENSOR_IMPLAUSIBLE);
  assert(!out.valve_to_boiler);
  assert(!out.element_on);
  assert(!out.hp_dhw_request);
}

// Zonder coil-sensoren start een gewone cyclus en bereikt die de HP-fase.
void test_cycle_without_coil_sensors() {
  const Config cfg;
  Controller c;
  uint32_t now = 1000U;
  Outputs out;
  for (int i = 0; i < 20 && out.state != State::DHW_HEAT_PUMP; ++i, now += 2000U) {
    out = c.tick(fe_inputs(now, 40.0f, 30.0f), cfg);
    assert(out.state != State::FAULT);
  }
  assert(out.state == State::DHW_HEAT_PUMP);
  assert(out.hp_dhw_request);
  assert(out.valve_to_boiler);
}

// Een ontbrekende bottom-waarde is "niet geïnstalleerd", geen storing.
void test_missing_bottom_is_not_a_fault() {
  const Config cfg;
  Controller c;
  const Outputs out = c.tick(fe_inputs(1000U, 50.0f, NAN), cfg);
  assert(out.state != State::FAULT);
  assert(out.fault == Fault::NONE);
}

// Tank top is verplicht: zonder geldige waarde gaat alles fail-safe uit.
void test_missing_top_is_fail_safe() {
  const Config cfg;
  Controller c;
  assert_fail_safe(c.tick(fe_inputs(1000U, NAN, 30.0f), cfg));
}

// Ook midden in een cyclus valt alles af als tank top wegvalt.
void test_top_lost_during_cycle_is_fail_safe() {
  const Config cfg;
  Controller c;
  uint32_t now = 1000U;
  Outputs out;
  for (int i = 0; i < 20 && out.state != State::DHW_HEAT_PUMP; ++i, now += 2000U) {
    out = c.tick(fe_inputs(now, 40.0f, 30.0f), cfg);
  }
  assert(out.state == State::DHW_HEAT_PUMP);
  assert_fail_safe(c.tick(fe_inputs(now, NAN, 30.0f), cfg));
}

// Een bottom die ver boven top uitkomt (verwisselde of losgeraakte sensor)
// is onplausibel.
void test_bottom_far_above_top_is_fail_safe() {
  const Config cfg;
  Controller c;
  assert_fail_safe(c.tick(fe_inputs(1000U, 40.0f, 53.0f), cfg));
}

}  // namespace

int main() {
  test_cycle_without_coil_sensors();
  test_missing_bottom_is_not_a_fault();
  test_missing_top_is_fail_safe();
  test_top_lost_during_cycle_is_fail_safe();
  test_bottom_far_above_top_is_fail_safe();
  return 0;
}
