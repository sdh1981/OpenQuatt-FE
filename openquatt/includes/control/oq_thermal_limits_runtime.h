#pragma once

#include "oq_thermal_limits_logic.h"
#if OQ_FE_TARGET
#include "../fe/oq_fe_dhw_bridge.h"  // FE: DHW-watergrens zolang DHW de aanvoer heeft
#endif

namespace oq_thermal_limits_runtime {

class Runtime {
 public:
  void tick() {
#if OQ_FE_TARGET
    // FE: tijdens warm water gaat de aanvoer naar de boiler; dan geldt het DHW-maximum.
    const float max_c = oq_fe_dhw_bridge::water_limit_max_c(
        oq_fe_dhw_bridge::state().dhw_owns_supply, id(oq_dhw_max_water_temp_c).state, id(max_water_temp_limit_c).state);
#else
    const float max_c = id(max_water_temp_limit_c).state;
#endif
    oq_thermal_limits::update(
        state_, {(uint32_t)millis(), id(water_supply_temp_selected).state, max_c, id(oq_control_mode_code)});
    id(oq_water_temp_limit_factor) = state_.limit_factor;
    id(oq_water_temp_boiler_inhibit_active) = state_.boiler_inhibit;
    id(oq_water_temp_trip_active) = state_.trip;
    id(oq_water_temp_hard_trip_active) = state_.hard_trip;
  }

 private:
  oq_thermal_limits::State state_{};
};

inline Runtime& runtime() {
  static Runtime value;
  return value;
}

}  // namespace oq_thermal_limits_runtime
