#include <cassert>
#include <iostream>
#include "components/openquatt_modbus/OpenQuattModbusClientHub.h"

int main() {
  using esphome::openquatt_modbus::OpenQuattModbusClientHub;
  OpenQuattModbusClientHub hub;
  int owner = 1, stranger = 2;
  const uint8_t frame[]{1, 3, 0, 0, 0};
  unsigned cancelled = 0;
  assert(!hub.begin_maintenance(nullptr));
  hub.queue([&] { ++cancelled; });
  hub.in_flight();
  assert(hub.begin_maintenance(&owner));
  assert(!hub.begin_maintenance(&stranger));
  hub.loop();
  assert(cancelled == 1 && !hub.maintenance_ready(&owner));
  assert(hub.ordinary_sends == 0);
  assert(!hub.maintenance_send(&owner, frame, sizeof(frame)));
  hub.response_finished = true;
  hub.loop();
  assert(hub.maintenance_ready(&owner));
  assert(!hub.maintenance_ready(&stranger));
  assert(!hub.maintenance_send(&stranger, frame, sizeof(frame)));
  hub.end_maintenance(&stranger);
  assert(hub.maintenance_ready(&owner));
  hub.queue([&] { ++cancelled; });
  hub.loop();
  assert(cancelled == 2 && hub.ordinary_sends == 0);
  assert(hub.maintenance_send(&owner, frame, sizeof(frame)));
  assert(hub.output.size() == sizeof(frame));
  hub.input.push_back(42);
  const unsigned loops = hub.normal_loops;
  hub.loop();
  assert(hub.normal_loops == loops);  // No competing UART consumer.
  uint8_t byte = 0;
  assert(!hub.maintenance_read(&stranger, byte));
  assert(hub.maintenance_read(&owner, byte) && byte == 42);
  assert(!hub.maintenance_read(&owner, byte));
  hub.line_busy = true;
  assert(!hub.maintenance_send(&owner, frame, sizeof(frame)));
  hub.line_busy = false;
  hub.delay_us = 10;
  assert(!hub.maintenance_send(&owner, frame, sizeof(frame)));
  hub.delay_us = 0;
  hub.queue([&] {
    ++cancelled;
    hub.queue([&] { ++cancelled; });
  });
  assert(hub.end_maintenance(&owner));
  assert(cancelled == 4 && !hub.maintenance_ready(&owner));
  hub.loop();
  assert(hub.ordinary_sends == 0);  // No replay of commands accumulated during flashing.
  hub.queue();
  hub.loop();
  assert(hub.ordinary_sends == 1);
  // A cancellation producer which will not stop must never regain the bus.
  std::function<void()> producer;
  producer = [&] { hub.queue(producer); };
  hub.queue(producer);
  assert(hub.begin_maintenance(&owner));
  hub.loop();
  assert(!hub.end_maintenance(&owner));
  assert(hub.maintenance_ready(&owner));
  hub.loop();
  assert(hub.ordinary_sends == 1);
  std::cout << "PASS integrated RS485 lease: drain, ownership, isolation, timing, cancellation, release\n";
}
