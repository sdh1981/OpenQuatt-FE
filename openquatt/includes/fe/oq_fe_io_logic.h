// OpenQuatt FE - eigenaarschap van R1/R2 en de bankproef-uitgangen.
//
// Upstream schrijft R1 (ketel) en R2 (hulprelais) via hun eigen runtimes. In het
// FE-target lopen die schrijfacties via een template-output naar deze logica,
// die per relais bepaalt wat er fysiek op de pin komt. Zo hoeft geen upstream-
// bestand te veranderen en bestaat er per pin precies één eigenaar.
#pragma once

#include <math.h>
#include <stdint.h>

namespace oq_fe_io {

// R1: is er een bijverwarmer (CV-ketel) aangesloten, dan volgt R1 upstream.
// Zonder bijverwarmer (Full Electric) is R1 het DHW-element. Upstream houdt de
// ketelvraag dan zelf al laag; het element valt bij het omzetten van de
// schakelaar direct af.
inline bool r1_output(bool aux_heat_source_present, bool boiler_request, bool element_request) {
  return aux_heat_source_present ? boiler_request : element_request;
}

// R2: als DHW-klep in gebruik, dan volgt R2 de klepvraag; anders upstream.
// Onbekrachtigd = CV-stand, dus elke storing laat de klep op CV vallen.
inline bool r2_output(bool r2_is_dhw_valve, bool aux_relay_request, bool valve_request) {
  return r2_is_dhw_valve ? valve_request : aux_relay_request;
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

// Het element mag alleen proefdraaien als R1 van FE is, tank top een geldige
// waarde heeft en de tank nog ruim onder de grens van het element zit.
inline bool element_test_allowed(bool aux_heat_source_present, float tank_top_c, float max_top_c) {
  return !aux_heat_source_present && plausible_tank_temp(tank_top_c) && tank_top_c < max_top_c;
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
