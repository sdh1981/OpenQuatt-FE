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
// Strategiecode in oq_request; upstream gebruikt 0..4.
constexpr int STRATEGY_DHW = 5;

struct State {
  // DHW wil de regeling: HP-vraag of klep in beweging (CV-voorrang blokkeren).
  bool mode_requested = false;
  int hp1_level = 0;
  int hp2_level = 0;
  // 1 = HP1, 2 = HP2, 3 = beide, 0 = geen.
  int owner_hp = 0;
  const char* reason = "dhw_idle";
  float flow_setpoint_lph = NAN;
};

inline State& state() {
  static State value;
  return value;
}

inline void clear() { state() = State{}; }

// Basisdoel van de supervisory: DHW gaat voor koelen en verwarmen, maar een
// flow-interlock houdt de regeling in CM1.
inline int base_target(int upstream_base_target, bool dhw_requested, bool flow_interlock) {
  if (!dhw_requested) return upstream_base_target;
  return flow_interlock ? 1 : CM_DHW;
}

}  // namespace oq_fe_dhw_bridge
