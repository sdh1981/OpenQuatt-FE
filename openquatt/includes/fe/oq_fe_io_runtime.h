// OpenQuatt FE - runtime voor R1/R2 en de bankproeven (alleen in het FE-target).
#pragma once

#include "oq_fe_io_logic.h"

// De map-include neemt deze header in elk target op; alleen het FE-target heeft
// de entiteiten waar de runtime naar verwijst.
#if OQ_FE_TARGET
namespace oq_fe_io_runtime {

struct TickConfig {
  uint32_t valve_test_max_ms;
  uint32_t element_test_max_ms;
  float element_test_max_top_c;
};

class Runtime {
 public:
  // Upstream schrijft zijn ketel- en hulprelaisvraag naar de template-outputs in
  // fe_io.yaml. Full Electric heeft geen ketel en geen hulprelais: alleen loggen.
  void set_boiler_request(bool on) {
    if (on) ESP_LOGD("fe.io", "Upstream boiler request ignored (Full Electric)");
  }

  void set_aux_relay_request(bool on) {
    if (on) ESP_LOGD("fe.io", "Upstream aux relay request ignored (Full Electric)");
  }

  // Fase 4: de DHW-regeling meldt hier zijn klep- en elementvraag.
  void set_dhw_requests(bool valve, bool element) {
    dhw_valve_request_ = valve;
    dhw_element_request_ = element;
    apply_();
  }

  // Bankproeven alleen als de DHW-regeling stilstaat (IDLE_CV, geen fout).
  void set_tests_allowed(bool allowed) { tests_allowed_ = allowed; }

  void tick(const TickConfig& cfg) {
    const uint32_t now_ms = (uint32_t)millis();
    // Full Electric: de CV-ketel staat altijd uit. Upstream schakelt dan zelf
    // ketel-assist, ketel-terugval en OpenTherm naar de ketel af.
    if (id(oq_aux_heat_source_present).state) {
      ESP_LOGW("fe.io", "Auxiliary heat source is not available on Full Electric; switching it off");
      id(oq_aux_heat_source_present).turn_off();
    }

    const auto valve =
        oq_fe_io::update_test(valve_test_, id(fe_test_dhw_valve).state, tests_allowed_, now_ms, cfg.valve_test_max_ms);
    const auto element = oq_fe_io::update_test(
        element_test_, id(fe_test_dhw_element).state,
        tests_allowed_ && oq_fe_io::element_test_allowed(id(fe_dhw_tank_top).state, cfg.element_test_max_top_c), now_ms,
        cfg.element_test_max_ms);
    valve_test_on_ = valve.on;
    element_test_on_ = element.on;
    if (valve.release_request) {
      ESP_LOGI("fe.io", "DHW valve test %s", oq_fe_io::test_status_text(valve.status));
      id(fe_test_dhw_valve).turn_off();
    }
    if (element.release_request) {
      ESP_LOGI("fe.io", "DHW element test %s", oq_fe_io::test_status_text(element.status));
      id(fe_test_dhw_element).turn_off();
    }
    apply_();

    publish_text_(id(fe_dhw_valve_test_status), valve_status_, oq_fe_io::test_status_text(valve.status));
    publish_text_(id(fe_dhw_element_test_status), element_status_, oq_fe_io::test_status_text(element.status));
    const auto position =
        oq_fe_io::valve_position(id(fe_dhw_valve_feedback).has_state(), id(fe_dhw_valve_feedback).state);
    publish_text_(id(fe_dhw_valve_position), position_, oq_fe_io::valve_position_text(position));
  }

 private:
  void apply_() {
    const bool r1 = oq_fe_io::r1_valve_output(dhw_valve_request_, valve_test_on_);
    const bool r2 = oq_fe_io::r2_element_output(dhw_element_request_, element_test_on_);
    write_(id(fe_r1_gpio_out), r1, r1_written_, r1_known_, "R1 valve");
    write_(id(fe_r2_gpio_out), r2, r2_written_, r2_known_, "R2 element");
    if (!id(fe_dhw_element_active).has_state() || id(fe_dhw_element_active).state != r2) {
      id(fe_dhw_element_active).publish_state(r2);
    }
  }

  template <typename Output>
  static void write_(Output& output, bool on, bool& written, bool& known, const char* name) {
    if (known && written == on) return;
    if (on) {
      output.turn_on();
    } else {
      output.turn_off();
    }
    ESP_LOGD("fe.io", "%s %s", name, on ? "ON" : "OFF");
    written = on;
    known = true;
  }

  template <typename Sensor>
  static void publish_text_(Sensor& sensor, const char*& last, const char* text) {
    if (last == text) return;
    sensor.publish_state(text);
    last = text;
  }

  bool tests_allowed_ = false;
  bool dhw_valve_request_ = false;
  bool dhw_element_request_ = false;
  bool valve_test_on_ = false;
  bool element_test_on_ = false;
  oq_fe_io::TestState valve_test_{};
  oq_fe_io::TestState element_test_{};
  bool r1_written_ = false;
  bool r1_known_ = false;
  bool r2_written_ = false;
  bool r2_known_ = false;
  const char* valve_status_ = nullptr;
  const char* element_status_ = nullptr;
  const char* position_ = nullptr;
};

inline Runtime& runtime() {
  static Runtime value;
  return value;
}

}  // namespace oq_fe_io_runtime
#endif
