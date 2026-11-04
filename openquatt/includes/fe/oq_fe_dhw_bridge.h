// OpenQuatt FE - smalle naad tussen de DHW-regeling en het upstream-regelhart.
//
// De FE DHW-runtime schrijft hier zijn vraag; de upstream-runtimes lezen hem
// achter #if OQ_FE_TARGET. Geen ESPHome-afhankelijkheden, zodat upstream-logica
// en host-tests deze header kunnen opnemen.
#pragma once

#include <math.h>
#include <stdint.h>

namespace oq_fe_dhw_bridge {

// Control mode voor warm water (afgestemd met upstream, 2026-10-07).
constexpr int CM_DHW = 10;
// Control mode voor element-only: alleen het element, HP's en pomp uit.
constexpr int CM_ELEMENT_ONLY = 11;
// Strategiecode in oq_request; upstream gebruikt 0..4.
constexpr int STRATEGY_DHW = 5;

struct State {
  // DHW wil de regeling: HP-vraag of klep in beweging (CV-voorrang blokkeren).
  bool mode_requested = false;
  // Schakelaar "DHW Element Only Heating" staat aan.
  bool element_only_requested = false;
  int hp1_level = 0;
  int hp2_level = 0;
  // 1 = HP1, 2 = HP2, 3 = beide, 0 = geen.
  int owner_hp = 0;
  const char* reason = "dhw_idle";
  float flow_setpoint_lph = NAN;
  // De klep staat bevestigd op DHW en FE vraagt hem daar: het aanvoerwater gaat
  // naar de boilerspiraal, niet naar het CV-circuit. Dan geldt de DHW-watergrens.
  bool dhw_owns_supply = false;
};

inline State& state() {
  static State value;
  return value;
}

inline void clear() { state() = State{}; }

// Basisdoel van de supervisory. Voorrang: DHW (ook legionella en een lopende
// cyclus) > vorst > element-only > upstream. Een flow-interlock houdt DHW in CM1.
inline int base_target(int upstream_base_target, bool dhw_requested, bool element_only, bool flow_interlock,
                       bool frost) {
  if (dhw_requested) return flow_interlock ? 1 : CM_DHW;
  if (element_only) return frost ? 98 : CM_ELEMENT_ONLY;
  return upstream_base_target;
}

// Maximale watertemperatuur voor upstream's watergrens (oq_thermal_limits). Het
// CV-maximum beschermt het CV-/vloercircuit; zolang DHW het aanvoercircuit heeft,
// gaat het water naar de boiler en geldt het DHW-maximum. Zonder geldig
// DHW-maximum blijft het CV-maximum staan.
inline float water_limit_max_c(bool dhw_owns_supply, float dhw_max_c, float cv_max_c) {
  return (dhw_owns_supply && isfinite(dhw_max_c)) ? dhw_max_c : cv_max_c;
}

// Na een wissel van het geldende maximum telt de noodstop pas na settle_ms weer
// mee: upstream rekent hem periodiek opnieuw, dus tot dan kan hij nog bij de
// vorige grens horen. changed_ms == 0: nooit gewisseld.
inline bool water_limit_settled(uint32_t changed_ms, uint32_t now_ms, uint32_t settle_ms) {
  return changed_ms == 0 || static_cast<uint32_t>(now_ms - changed_ms) >= settle_ms;
}

// Modi waarin warmtepompen of de pomp actief kunnen zijn: element-only gaat
// daarvandaan eerst via de CM1-naloop.
inline bool needs_postflow_before_element_only(int current_mode) {
  return current_mode == 2 || current_mode == 3 || current_mode == 4 || current_mode == 5 || current_mode == CM_DHW;
}

}  // namespace oq_fe_dhw_bridge
