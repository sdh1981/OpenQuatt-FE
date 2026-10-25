// OpenQuatt FE - bewaking van een doorstroomelement vóór de 3-wegklep
// (fe/element-optie-voorstel.md, fase A). Geen ESPHome-afhankelijkheden.
#pragma once

#include <math.h>
#include <stdint.h>

namespace oq_fe_element {

enum class Placement : uint8_t { TANK = 0, INLINE = 1 };

struct InlineConfig {
  float min_flow_lph = 400.0f;
  float max_supply_c = 75.0f;
  float supply_hysteresis_k = 5.0f;
  uint32_t flow_confirm_ms = 10000;   // flow eerst zo lang aaneen boven het minimum
  uint32_t afterrun_ms = 60000;       // pompen nadraaien na het uitschakelen
  uint32_t no_flow_fault_ms = 60000;  // zo lang geen flow bij een elementvraag: storing
};

struct InlineOutput {
  bool element_allowed = false;  // element mag nu aan
  bool circulation = false;      // klep op Boiler en pompen aan (vraag of nadraaien)
  bool no_flow_fault = false;    // te lang geen flow terwijl het element gevraagd wordt
  bool flow_ok = false;          // flow nu boven het minimum
  bool supply_blocked = false;   // aanvoer te warm of ongeldig
};

inline bool flow_ok(float flow_lph, float min_flow_lph) { return isfinite(flow_lph) && flow_lph >= min_flow_lph; }

class InlineGuard {
 public:
  InlineOutput update(bool requested, float flow_lph, float supply_c, uint32_t now_ms, const InlineConfig& cfg) {
    InlineOutput out;
    out.flow_ok = flow_ok(flow_lph, cfg.min_flow_lph);

    // Flow pas bevestigd na een aaneengesloten periode boven het minimum.
    if (!out.flow_ok) {
      flow_since_valid_ = false;
    } else if (!flow_since_valid_) {
      flow_since_valid_ = true;
      flow_since_ms_ = now_ms;
    }
    const bool flow_confirmed =
        flow_since_valid_ && static_cast<uint32_t>(now_ms - flow_since_ms_) >= cfg.flow_confirm_ms;

    // Aanvoergrens met hysterese; een ongeldige aanvoer blokkeert altijd.
    if (!isfinite(supply_c) || supply_c >= cfg.max_supply_c) {
      supply_blocked_ = true;
    } else if (supply_c <= cfg.max_supply_c - cfg.supply_hysteresis_k) {
      supply_blocked_ = false;
    }
    out.supply_blocked = supply_blocked_;

    out.element_allowed = requested && flow_confirmed && !supply_blocked_;

    // Nadraaien: na een element dat aan stond nog even circuleren.
    if (element_was_on_ && !out.element_allowed) {
      afterrun_until_valid_ = true;
      afterrun_until_ms_ = now_ms + cfg.afterrun_ms;
    }
    element_was_on_ = out.element_allowed;
    const bool afterrun = afterrun_until_valid_ && static_cast<int32_t>(afterrun_until_ms_ - now_ms) > 0;
    if (!afterrun) afterrun_until_valid_ = false;
    out.circulation = requested || afterrun;

    // Storing: elementvraag zonder flow, lang genoeg om de pompen te laten aanlopen.
    if (!requested || out.flow_ok) {
      no_flow_since_valid_ = false;
    } else if (!no_flow_since_valid_) {
      no_flow_since_valid_ = true;
      no_flow_since_ms_ = now_ms;
    }
    out.no_flow_fault =
        no_flow_since_valid_ && static_cast<uint32_t>(now_ms - no_flow_since_ms_) >= cfg.no_flow_fault_ms;
    return out;
  }

  void reset() { *this = InlineGuard{}; }

 private:
  bool flow_since_valid_ = false;
  uint32_t flow_since_ms_ = 0;
  bool supply_blocked_ = true;  // tot er een geldige, koele aanvoer gezien is
  bool element_was_on_ = false;
  bool afterrun_until_valid_ = false;
  uint32_t afterrun_until_ms_ = 0;
  bool no_flow_since_valid_ = false;
  uint32_t no_flow_since_ms_ = 0;
};

}  // namespace oq_fe_element
