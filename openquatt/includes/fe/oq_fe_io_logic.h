// OpenQuatt FE - eigenaarschap van R1/R2 en de bankproef-uitgangen.
//
// Full Electric heeft geen CV-ketel en geen hulprelais: FE is de enige eigenaar
// van beide relais. R1 stuurt de 3-wegklep, R2 het element. Upstream schrijft
// zijn ketel- en hulprelaisvraag nog wel naar een template-output met de oude
// id, maar die vraag komt in FE nooit op een pin.
#pragma once

#include <math.h>
#include <stdint.h>

namespace oq_fe_io {

// R1 (COM/NO): 3-wegklep. Bekrachtigd = DHW-stand, onbekrachtigd = CV-stand,
// dus elke storing of herstart laat de klep op CV vallen.
inline bool r1_valve_output(bool dhw_valve_request, bool valve_test) { return dhw_valve_request || valve_test; }

// R2 (NC/COM/NO, contactor op NO): het 3 kW-element.
inline bool r2_element_output(bool dhw_element_request, bool element_test) {
  return dhw_element_request || element_test;
}

// Een DS18B20 meldt na een spanningsreset eenmalig exact 85,0 C. Die waarde
// telt niet: de DHW-logica zou haar als plausibele tank-temperatuur lezen.
inline bool ds18b20_power_on_value(float temperature_c) { return temperature_c == 85.0f; }

inline bool plausible_tank_temp(float temperature_c) {
  return !isnan(temperature_c) && temperature_c >= -10.0f && temperature_c <= 85.0f;
}

// Bankproef-uitgang: aan zolang gevraagd én toegestaan, en nooit langer dan
// max_ms. Na verlopen of weigeren moet de vraag eerst weer los voordat een
// nieuwe proef kan starten.
struct TestState {
  bool on = false;
  bool latched_off = false;
  uint32_t since_ms = 0;
};

enum class TestStatus : uint8_t { OFF, ON, EXPIRED, NOT_ALLOWED };

struct TestDecision {
  bool on = false;
  bool release_request = false;  // de runtime zet de proefschakelaar terug
  TestStatus status = TestStatus::OFF;
};

inline TestDecision update_test(TestState& state, bool requested, bool allowed, uint32_t now_ms, uint32_t max_ms) {
  TestDecision result;
  if (!requested) {
    state = TestState{};
    return result;
  }
  if (state.latched_off) {
    result.release_request = true;
    result.status = TestStatus::EXPIRED;
    return result;
  }
  if (!allowed) {
    state = TestState{};
    state.latched_off = true;
    result.release_request = true;
    result.status = TestStatus::NOT_ALLOWED;
    return result;
  }
  if (!state.on) {
    state.on = true;
    state.since_ms = now_ms;
  }
  if ((uint32_t)(now_ms - state.since_ms) >= max_ms) {
    state = TestState{};
    state.latched_off = true;
    result.release_request = true;
    result.status = TestStatus::EXPIRED;
    return result;
  }
  result.on = true;
  result.status = TestStatus::ON;
  return result;
}

// Het element mag alleen proefdraaien als tank top een geldige waarde heeft en
// de tank nog ruim onder de grens van het element zit.
inline bool element_test_allowed(float tank_top_c, float max_top_c) {
  return plausible_tank_temp(tank_top_c) && tank_top_c < max_top_c;
}

enum class ValvePosition : uint8_t { UNKNOWN, CV, DHW };

// Terugmelding: het hulpcontact van de klep sluit in de DHW-stand.
inline ValvePosition valve_position(bool feedback_valid, bool contact_closed) {
  if (!feedback_valid) return ValvePosition::UNKNOWN;
  return contact_closed ? ValvePosition::DHW : ValvePosition::CV;
}

inline const char* valve_position_text(ValvePosition position) {
  switch (position) {
    case ValvePosition::CV:
      return "CV";
    case ValvePosition::DHW:
      return "DHW";
    default:
      return "Unknown";
  }
}

inline const char* test_status_text(TestStatus status) {
  switch (status) {
    case TestStatus::ON:
      return "On";
    case TestStatus::EXPIRED:
      return "Expired";
    case TestStatus::NOT_ALLOWED:
      return "Not allowed";
    default:
      return "Off";
  }
}

}  // namespace oq_fe_io
