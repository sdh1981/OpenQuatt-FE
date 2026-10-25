// OpenQuatt FE - host-test voor de bewaking van een doorstroomelement.
#include <assert.h>
#include <math.h>

#include "../../openquatt/includes/fe/oq_dhw_controller_logic.h"
#include "../../openquatt/includes/fe/oq_fe_element_logic.h"

using namespace oq_fe_element;

namespace {

const InlineConfig kCfg{};

void test_needs_confirmed_flow() {
  InlineGuard guard;
  // Geen flow: niet aan, wel circuleren zodat de pompen aanlopen.
  auto out = guard.update(true, 0.0f, 40.0f, 1000, kCfg);
  assert(!out.element_allowed);
  assert(out.circulation);
  // Flow boven het minimum, maar nog geen 10 s.
  out = guard.update(true, 600.0f, 40.0f, 2000, kCfg);
  assert(!out.element_allowed);
  out = guard.update(true, 600.0f, 40.0f, 11000, kCfg);
  assert(!out.element_allowed);
  out = guard.update(true, 600.0f, 40.0f, 12000, kCfg);
  assert(out.element_allowed);
}

void test_flow_drop_switches_off_immediately() {
  InlineGuard guard;
  guard.update(true, 600.0f, 40.0f, 0, kCfg);
  assert(guard.update(true, 600.0f, 40.0f, 10000, kCfg).element_allowed);
  auto out = guard.update(true, 390.0f, 40.0f, 12000, kCfg);
  assert(!out.element_allowed);
  // Terug boven het minimum: opnieuw 10 s bevestigen.
  out = guard.update(true, 600.0f, 40.0f, 14000, kCfg);
  assert(!out.element_allowed);
  out = guard.update(true, 600.0f, 40.0f, 24000, kCfg);
  assert(out.element_allowed);
  // NAN-flow telt als geen flow.
  assert(!guard.update(true, NAN, 40.0f, 26000, kCfg).element_allowed);
}

void test_supply_limit_with_hysteresis() {
  InlineGuard guard;
  guard.update(true, 600.0f, 40.0f, 0, kCfg);
  assert(guard.update(true, 600.0f, 74.9f, 10000, kCfg).element_allowed);
  assert(!guard.update(true, 600.0f, 75.0f, 12000, kCfg).element_allowed);
  assert(!guard.update(true, 600.0f, 72.0f, 14000, kCfg).element_allowed);  // binnen de hysterese
  assert(guard.update(true, 600.0f, 70.0f, 16000, kCfg).element_allowed);
  // Ongeldige aanvoer blokkeert.
  assert(!guard.update(true, 600.0f, NAN, 18000, kCfg).element_allowed);
  assert(!guard.update(true, 600.0f, 72.0f, 20000, kCfg).element_allowed);
  assert(guard.update(true, 600.0f, 69.0f, 22000, kCfg).element_allowed);
}

void test_afterrun() {
  InlineGuard guard;
  guard.update(true, 600.0f, 40.0f, 0, kCfg);
  assert(guard.update(true, 600.0f, 40.0f, 10000, kCfg).element_allowed);
  // Vraag weg: element uit, 60 s nadraaien.
  auto out = guard.update(false, 600.0f, 40.0f, 20000, kCfg);
  assert(!out.element_allowed);
  assert(out.circulation);
  assert(guard.update(false, 600.0f, 40.0f, 79000, kCfg).circulation);
  assert(!guard.update(false, 600.0f, 40.0f, 80000, kCfg).circulation);
  // Zonder dat het element aan stond: geen nadraaien.
  InlineGuard idle;
  assert(!idle.update(false, 600.0f, 40.0f, 1000, kCfg).circulation);
}

void test_no_flow_fault() {
  InlineGuard guard;
  assert(!guard.update(true, 0.0f, 40.0f, 0, kCfg).no_flow_fault);
  assert(!guard.update(true, 0.0f, 40.0f, 59000, kCfg).no_flow_fault);
  assert(guard.update(true, 0.0f, 40.0f, 60000, kCfg).no_flow_fault);
  // Flow terug of vraag weg: geen storing meer van de guard.
  assert(!guard.update(true, 600.0f, 40.0f, 61000, kCfg).no_flow_fault);
  assert(!guard.update(false, 0.0f, 40.0f, 62000, kCfg).no_flow_fault);
}

void test_millis_wrap() {
  InlineGuard guard;
  const uint32_t t0 = 0xFFFFF000u;
  guard.update(true, 600.0f, 40.0f, t0, kCfg);
  assert(guard.update(true, 600.0f, 40.0f, t0 + 10000u, kCfg).element_allowed);
  auto out = guard.update(false, 600.0f, 40.0f, t0 + 12000u, kCfg);
  assert(out.circulation);
  assert(guard.update(false, 600.0f, 40.0f, t0 + 71000u, kCfg).circulation);
  assert(!guard.update(false, 600.0f, 40.0f, t0 + 72000u, kCfg).circulation);
}

void test_controller_latches_no_flow_fault() {
  oq_dhw::Controller controller;
  oq_dhw::Config cfg;
  oq_dhw::Inputs in;
  in.now_ms = 1000;
  in.tank_top_c = 50.0f;
  in.tank_bottom_c = 45.0f;
  in.legionella_seeded = true;
  in.element_no_flow = true;
  const auto out = controller.tick(in, cfg);
  assert(out.state == oq_dhw::State::FAULT);
  assert(out.fault == oq_dhw::Fault::ELEMENT_NO_FLOW);
  assert(!out.element_on);
  assert(!out.valve_to_boiler);
}

}  // namespace

int main() {
  test_needs_confirmed_flow();
  test_flow_drop_switches_off_immediately();
  test_supply_limit_with_hysteresis();
  test_afterrun();
  test_no_flow_fault();
  test_millis_wrap();
  test_controller_latches_no_flow_fault();
  return 0;
}
