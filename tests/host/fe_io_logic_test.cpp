// Host-test voor het R1/R2-eigenaarschap en de bankproef-uitgangen van FE.
//
//   c++ -std=c++17 -Wall -Wextra -Werror tests/host/fe_io_logic_test.cpp -o /tmp/t && /tmp/t

#undef NDEBUG
#include <assert.h>
#include <math.h>
#include <stdint.h>

#include "../../openquatt/includes/fe/oq_fe_io_logic.h"

namespace {

using namespace oq_fe_io;

// Met een ketel volgt R1 alleen de ketelvraag; het element kan R1 niet pakken.
void test_r1_follows_boiler_when_source_present() {
  assert(r1_output(true, true, false));
  assert(!r1_output(true, false, true));
}

// Full Electric: R1 is het element, een (verdwaalde) ketelvraag telt niet.
void test_r1_is_element_without_source() {
  assert(r1_output(false, false, true));
  assert(!r1_output(false, true, false));
}

void test_r2_ownership() {
  assert(r2_output(true, false, true));
  assert(!r2_output(true, true, false));
  assert(r2_output(false, true, false));
  assert(!r2_output(false, false, true));
}

void test_ds18b20_power_on_value() {
  assert(ds18b20_power_on_value(85.0f));
  assert(!ds18b20_power_on_value(84.9f));
  assert(!ds18b20_power_on_value(NAN));
}

void test_element_test_guard() {
  assert(element_test_allowed(false, 50.0f, 65.0f));
  assert(!element_test_allowed(true, 50.0f, 65.0f));  // R1 is van de ketel
  assert(!element_test_allowed(false, NAN, 65.0f));   // tank top onbekend
  assert(!element_test_allowed(false, 65.0f, 65.0f));
}

// Een proef loopt tot de maximale duur en valt dan af; pas na loslaten kan
// een nieuwe proef starten.
void test_test_output_expires() {
  TestState s;
  auto d = update_test(s, true, true, 1000U, 5000U);
  assert(d.on && d.status == TestStatus::ON);
  d = update_test(s, true, true, 5999U, 5000U);
  assert(d.on);
  d = update_test(s, true, true, 6000U, 5000U);
  assert(!d.on && d.release_request && d.status == TestStatus::EXPIRED);
  d = update_test(s, true, true, 7000U, 5000U);  // blijft uit zolang de vraag staat
  assert(!d.on && d.release_request);
  d = update_test(s, false, true, 8000U, 5000U);
  assert(!d.on && !d.release_request && d.status == TestStatus::OFF);
  d = update_test(s, true, true, 9000U, 5000U);
  assert(d.on);
}

// Valt de toestemming weg tijdens een proef, dan gaat de uitgang meteen uit en
// komt pas terug na een nieuwe vraag.
void test_test_output_withdrawn() {
  TestState s;
  auto d = update_test(s, true, true, 0U, 5000U);
  assert(d.on);
  d = update_test(s, true, false, 100U, 5000U);
  assert(!d.on && d.release_request && d.status == TestStatus::NOT_ALLOWED);
  d = update_test(s, true, true, 200U, 5000U);
  assert(!d.on);
}

// millis() loopt na 49,7 dagen over; de looptijd moet dat overleven.
void test_test_output_survives_millis_wrap() {
  TestState s;
  auto d = update_test(s, true, true, 0xFFFFF000U, 5000U);
  assert(d.on);
  d = update_test(s, true, true, 0x00000100U, 5000U);
  assert(d.on);
  d = update_test(s, true, true, 0x00000400U, 5000U);
  assert(!d.on && d.status == TestStatus::EXPIRED);
}

void test_valve_position() {
  assert(valve_position(false, true) == ValvePosition::UNKNOWN);
  assert(valve_position(true, true) == ValvePosition::DHW);
  assert(valve_position(true, false) == ValvePosition::CV);
}

}  // namespace

int main() {
  test_r1_follows_boiler_when_source_present();
  test_r1_is_element_without_source();
  test_r2_ownership();
  test_ds18b20_power_on_value();
  test_element_test_guard();
  test_test_output_expires();
  test_test_output_withdrawn();
  test_test_output_survives_millis_wrap();
  test_valve_position();
  return 0;
}
