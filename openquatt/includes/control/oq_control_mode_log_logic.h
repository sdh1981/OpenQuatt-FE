#pragma once

#include <stdint.h>

namespace oq_hp_supervisory {

struct ControlModeLogReasonCodes {
  uint8_t unknown = 0;
  uint8_t boiler_fallback = 0;
  uint8_t hp_recovered = 0;
  uint8_t fallback_blocked = 0;
  uint8_t commissioning = 0;
  uint8_t supervisory_override = 0;
  uint8_t frost_protection = 0;
  uint8_t cooling_request = 0;
  uint8_t heating_request = 0;
  uint8_t cooling_request_cleared = 0;
  uint8_t heating_request_cleared = 0;
  uint8_t hp_recovery_wait = 0;
  // OpenQuatt FE: CM10 (warm water) en CM11 (alleen element).
  uint8_t dhw_request = 0;
  uint8_t element_only = 0;
  uint8_t dhw_request_cleared = 0;
};

struct ControlModeLogSeverityCodes {
  uint8_t normal = 0;
  uint8_t limited = 0;
  uint8_t fault = 0;
};

struct ControlModeLogStateCodes {
  uint8_t idle = 0;
  uint8_t standby = 0;
  uint8_t active = 0;
  uint8_t fallback = 0;
};

struct ControlModeLogCodes {
  ControlModeLogReasonCodes reason;
  ControlModeLogSeverityCodes severity;
  ControlModeLogStateCodes state;
};

struct ControlModeLogInputs {
  int previous_mode = 0;
  int desired_mode = 0;
  bool fallback_requested = false;
  bool supervisory_override_transition = false;
  uint8_t cm1_event_reason = 0;
};

struct ControlModeLogDecision {
  uint8_t reason = 0;
  uint8_t severity = 0;
  uint8_t from_state = 0;
  uint8_t to_state = 0;
};

inline uint8_t control_mode_log_state(int mode, const ControlModeLogStateCodes& codes) {
  if (mode == 0) return codes.idle;
  if (mode == 1) return codes.standby;
  if (mode == 4) return codes.fallback;
  return codes.active;
}

inline ControlModeLogDecision classify_control_mode_transition(const ControlModeLogInputs& inputs,
                                                               const ControlModeLogCodes& codes) {
  ControlModeLogDecision decision;
  decision.reason = codes.reason.unknown;
  decision.severity = codes.severity.normal;
  decision.from_state = control_mode_log_state(inputs.previous_mode, codes.state);
  decision.to_state = control_mode_log_state(inputs.desired_mode, codes.state);

  if (inputs.desired_mode == 4) {
    decision.reason = codes.reason.boiler_fallback;
    decision.severity = codes.severity.fault;
  } else if (inputs.desired_mode == 100 || inputs.previous_mode == 100) {
    decision.reason = codes.reason.commissioning;
  } else if (inputs.supervisory_override_transition) {
    decision.reason = codes.reason.supervisory_override;
  } else if (inputs.desired_mode == 98) {
    decision.reason = codes.reason.frost_protection;
  } else if (inputs.desired_mode == 5) {
    decision.reason = codes.reason.cooling_request;
  } else if (inputs.desired_mode == 10) {
    decision.reason = codes.reason.dhw_request;
  } else if (inputs.desired_mode == 11) {
    decision.reason = codes.reason.element_only;
  } else if (inputs.previous_mode == 4) {
    if (inputs.desired_mode == 2 || inputs.desired_mode == 3) {
      decision.reason = codes.reason.hp_recovered;
    } else if (inputs.desired_mode == 1 && !inputs.fallback_requested &&
               inputs.cm1_event_reason != codes.reason.unknown) {
      decision.reason = inputs.cm1_event_reason;
    } else if (inputs.desired_mode == 1 && !inputs.fallback_requested) {
      decision.reason = codes.reason.hp_recovery_wait;
      decision.severity = codes.severity.limited;
    } else if (inputs.desired_mode == 0 && !inputs.fallback_requested) {
      decision.reason = codes.reason.heating_request_cleared;
    } else {
      decision.reason = codes.reason.fallback_blocked;
      decision.severity = codes.severity.limited;
    }
  } else if (inputs.desired_mode == 2 || inputs.desired_mode == 3) {
    decision.reason = codes.reason.heating_request;
  } else if (inputs.desired_mode == 1 && inputs.fallback_requested) {
    decision.reason = codes.reason.fallback_blocked;
    decision.severity = codes.severity.limited;
  } else if (inputs.cm1_event_reason != codes.reason.unknown) {
    decision.reason = inputs.cm1_event_reason;
  } else if ((inputs.desired_mode == 0 || inputs.desired_mode == 1) &&
             (inputs.previous_mode == 10 || inputs.previous_mode == 11)) {
    decision.reason = codes.reason.dhw_request_cleared;
  } else if (inputs.desired_mode == 0) {
    decision.reason =
        inputs.previous_mode == 5 ? codes.reason.cooling_request_cleared : codes.reason.heating_request_cleared;
  }
  return decision;
}

}  // namespace oq_hp_supervisory
