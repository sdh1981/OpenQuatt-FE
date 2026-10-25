#include <deque>
#include <iostream>
#include <vector>
#include "components/openquatt_odu_diagnostics/OpenQuattOduDiagnostics.h"

using namespace oq_odu_diagnostics;
using Hub = esphome::openquatt_modbus::OpenQuattModbusClientHub;
using Component = esphome::openquatt_odu_diagnostics::OpenQuattOduDiagnostics;

// Independent wire-CRC oracle, deliberately not the production CRC helper.
uint16_t checksum(const std::vector<uint8_t>& bytes) {
  uint16_t value = 65535;
  for (const auto byte : bytes) {
    value ^= byte;
    for (int i = 0; i != 8; ++i) {
      const bool low = value & 1;
      value /= 2;
      if (low) value ^= 40961;
    }
  }
  return value;
}
void seal(std::vector<uint8_t>& bytes) {
  const auto crc = checksum(bytes);
  bytes.push_back(crc & 255);
  bytes.push_back(crc >> 8);
}
std::vector<uint8_t> request(uint8_t slave, uint8_t fn, uint16_t reg, uint16_t words) {
  std::vector<uint8_t> data{slave, fn, uint8_t(reg >> 8), uint8_t(reg), uint8_t(words >> 8), uint8_t(words)};
  seal(data);
  return data;
}
std::vector<uint8_t> response(const uint8_t* req, bool exception = false) {
  if (exception) {
    std::vector<uint8_t> bytes{req[0], 0x83, 2};
    seal(bytes);
    return bytes;
  }
  const unsigned reg = unsigned(req[2]) * 256 + req[3];
  const unsigned words = unsigned(req[4]) * 256 + req[5];
  std::vector<uint8_t> bytes{req[0], 3, uint8_t(words * 2)};
  if (reg == 11219) {
    const std::string serial = "SIM-ODU-0000001";
    for (unsigned i = 0; i < 40; ++i) bytes.push_back(i < serial.size() ? serial[i] : 0);
  } else {
    const unsigned value = reg == 2122 ? 0x0201 : reg == 2127 ? 0x1037 : reg == 10000 ? 3 : 0;
    bytes.push_back(value >> 8);
    bytes.push_back(value & 255);
  }
  seal(bytes);
  return bytes;
}
struct Wire : Bus {
  std::deque<uint8_t> input;
  std::vector<std::vector<uint8_t>> output;
  bool available{true};
  bool send(const uint8_t* data, size_t size) override {
    assert(allowed_request(data, size, 1, 2));
    if (!available) return false;
    output.emplace_back(data, data + size);
    return true;
  }
  bool read(uint8_t& value) override {
    if (input.empty()) return false;
    value = input.front();
    input.pop_front();
    return true;
  }
  void feed(std::vector<uint8_t> data) { input.insert(input.end(), data.begin(), data.end()); }
};

int main() {
  unsigned scenarios = 0;
  // Hub policy tests cover every Modbus function and every one-bit mutation
  // of a permitted frame; a corrupt/re-CRC'd write must still be rejected.
  Hub hub;
  int owner, stranger;
  hub.configure_read_only_diagnostics(&owner, 1, 2);
  hub.queue();
  hub.loop();
  assert(hub.ordinary_sends == 0);
  assert(!hub.begin_maintenance(&stranger));
  assert(hub.begin_maintenance(&owner));
  hub.loop();
  assert(hub.maintenance_ready(&owner));
  for (unsigned fn = 0; fn < 256; ++fn) {
    const auto data = request(1, fn, 2122, 1);
    assert(hub.maintenance_send(&owner, data.data(), data.size()) == (fn == 3));
    ++scenarios;
  }
  for (const auto& q : QUERIES) {
    auto data = request(2, 3, q.address, q.words);
    assert(hub.maintenance_send(&owner, data.data(), data.size()));
    for (size_t bit = 0; bit < 64; ++bit) {
      auto bad = data;
      bad[bit / 8] ^= 1 << (bit % 8);
      assert(!hub.maintenance_send(&owner, bad.data(), bad.size()));
      ++scenarios;
    }
  }
  for (const auto& data : {request(0, 3, 2122, 1), request(3, 3, 2122, 1), request(1, 3, 3999, 1),
                           request(1, 3, 2122, 2), request(1, 6, 4004, 0x5AA5), request(1, 6, 9999, 1)}) {
    assert(!hub.maintenance_send(&owner, data.data(), data.size()));
    ++scenarios;
  }
  assert(!hub.maintenance_send(&owner, nullptr, 8));
  std::vector<uint8_t> flash_frame(137, 0xAA);
  flash_frame[0] = 1;
  flash_frame[1] = 0x15;
  assert(!hub.maintenance_send(&owner, flash_frame.data(), flash_frame.size()));
  ++scenarios;
  assert(!hub.end_maintenance(&owner));
  hub.configure_read_only_diagnostics(&stranger, 3, 4);  // Cannot replace policy.
  auto data = request(3, 3, 2122, 1);
  assert(!hub.maintenance_send(&stranger, data.data(), data.size()));
  hub.queue();
  hub.loop();
  assert(hub.ordinary_sends == 0);
  ++scenarios;

  // Responses: success, per-register exceptions, silence, CRC damage, wrong
  // slave, partial reply, noise+fragmentation, busy bus, and millis rollover.
  for (unsigned mode = 0; mode < 9; ++mode) {
    Wire wire;
    Reader reader(wire);
    uint32_t now = mode == 8 ? UINT32_MAX - 2500 : 0;
    assert(!reader.start(0, now));
    assert(!reader.start(248, now));
    assert(reader.start(1, now));
    assert(!reader.start(2, now));
    if (mode == 7) wire.available = false;
    size_t responded = 0;
    for (unsigned tick = 0; reader.active() && tick < 8000; ++tick) {
      reader.tick(now);
      now += 10;
      if (wire.output.size() > responded) {
        const auto& req = wire.output[responded++];
        assert(req[1] == 3 && req.size() == 8);
        auto reply = response(req.data(), mode == 1);
        if (mode == 2) continue;
        if (mode == 3) reply.back() ^= 1;
        if (mode == 4) {
          reply[0] = 2;
          reply.resize(reply.size() - 2);
          seal(reply);
        }
        if (mode == 5) reply.resize(2);
        if (mode == 6) wire.feed({0xAA, 0xBB, 0xCC});
        // Feed one byte per tick to prove fragmentation is supported.
        for (auto byte : reply) {
          wire.feed({byte});
          reader.tick(now);
          now += 1;
        }
      }
    }
    assert(!reader.active());
    for (const auto& r : reader.results) {
      const auto expected = mode == 1                ? Outcome::EXCEPTION
                            : mode == 2              ? Outcome::TIMEOUT
                            : mode >= 3 && mode <= 5 ? Outcome::CORRUPT
                            : mode == 7              ? Outcome::BUS_BUSY
                                                     : Outcome::OK;
      assert(r.outcome == expected);
      assert(r.attempts == (mode == 7 ? 0 : mode >= 2 && mode <= 5 ? 2 : 1));
      if (mode == 1) assert(r.exception == 2);
    }
    if (mode == 0 || mode == 6 || mode == 8) {
      assert(reader.results[0].value == 0x0201 && reader.results[1].value == 0x1037);
      assert(memcmp(reader.serial, "SIM-ODU-0000001", 14) == 0);
    }
    ++scenarios;
  }

  // Only the extended serial and boot registers are unsupported. Basic
  // identity must remain available and exceptions still prove a live link.
  {
    Wire wire;
    Reader reader(wire);
    assert(reader.start(2, 0));
    size_t done = 0;
    for (uint32_t now = 0; now < 20000; now += 10) {
      reader.tick(now);
      if (wire.output.size() > done) {
        const auto& req = wire.output[done++];
        const unsigned reg = unsigned(req[2]) * 256 + req[3];
        wire.feed(response(req.data(), reg == 11219 || reg == 10000));
      }
    }
    assert(!reader.active() && reader.received == 6);
    assert(reader.results[0].outcome == Outcome::OK && reader.results[1].value == 0x1037);
    assert(reader.results[2].outcome == Outcome::EXCEPTION && reader.results[5].outcome == Outcome::EXCEPTION);
    assert(reader.serial[0] == 0);
    ++scenarios;
  }
  // A late reply during the timeout quarantine is discarded, not assigned
  // to the next register. A subsequent good retry is accepted.
  {
    Wire wire;
    Reader reader(wire);
    assert(reader.start(1, 0));
    reader.tick(1000);
    assert(wire.output.size() == 1);
    reader.tick(1900);
    assert(reader.results[0].outcome == Outcome::TIMEOUT);
    wire.feed(response(wire.output[0].data()));
    reader.tick(2000);
    assert(reader.received == 0 && reader.discarded_bytes == 7);
    reader.tick(2900);
    assert(wire.output.size() == 1);
    reader.tick(3000);
    assert(wire.output.size() == 2);
    wire.feed(response(wire.output[1].data()));
    reader.tick(3010);
    assert(reader.results[0].outcome == Outcome::OK && reader.results[0].attempts == 2);
    ++scenarios;
  }

  // Compile and exercise the actual ESPHome wrapper + actual hub; mock only
  // SDK/web/UART dependencies. No persistent-storage API is linked.
  Hub real_hub;
  Component component;
  esphome::openquatt_web_auth::OpenQuattWebAuth auth;
  component.set_hub(&real_hub);
  component.set_web_auth(&auth);
  unsigned holds = 0;
  component.set_hold_control([&] { ++holds; });
  real_hub.configure_read_only_diagnostics(&component, 1, 0);
  real_hub.queue();
  real_hub.loop();
  assert(real_hub.ordinary_sends == 0);
  component.setup();
  real_hub.loop();
  component.loop();
  auto& handler = *esphome::web_server_base::server.handler;
  AsyncWebServerRequest req;
  assert(handler.canHandle(&req));
  handler.handleRequest(&req);
  assert(req.raw.response.find("\"state\":\"ready\"") != std::string::npos);
  assert(real_hub.output.empty());
  ++scenarios;
  for (const char* route :
       {"/openquatt/odu-firmware/start", "/openquatt/odu-firmware/image", "/openquatt/odu-diagnostics/write"}) {
    req.path = route;
    req.verb = HTTP_POST;
    assert(!handler.canHandle(&req));
    ++scenarios;
  }
  req.path = "/openquatt/odu-diagnostics/scan";
  req.verb = HTTP_POST;
  for (unsigned failure = 0; failure < 4; ++failure) {
    diagnostic_test::authenticated = failure != 0;
    req.headers["X-OQ-CSRF"] = failure == 1 ? "bad" : "test-token";
    req.headers["Origin"] = failure == 2 ? "http://foreign.test" : "http://local.test";
    req.hp = failure == 3 ? "2" : "1";
    req.raw = {};
    handler.handleRequest(&req);
    assert(req.raw.status == (failure == 0 ? "401" : failure == 3 ? "409" : "403"));
    ++scenarios;
  }
  req.hp = "1";
  req.raw = {};
  handler.handleRequest(&req);
  assert(req.raw.response.find("\"state\":\"reading\"") != std::string::npos);
  assert(!component.request_scan(1));
  size_t consumed = 0;
  for (diagnostic_test::now = 0; diagnostic_test::now < 20000; diagnostic_test::now += 10) {
    real_hub.queue();
    real_hub.loop();
    component.loop();
    if (real_hub.output.size() > consumed) {
      assert(real_hub.output.size() - consumed == 8);
      auto reply = response(real_hub.output.data() + consumed);
      consumed = real_hub.output.size();
      real_hub.input.insert(real_hub.input.end(), reply.begin(), reply.end());
    }
  }
  httpd_req_t status;
  component.write_status(&status);
  assert(status.response.find("\"state\":\"complete\"") != std::string::npos);
  assert(status.response.find("\"sent\":6,\"received\":6") != std::string::npos);
  assert(real_hub.ordinary_sends == 0 && real_hub.output.size() == 48 && holds > 100);
  assert(!real_hub.end_maintenance(&component));
  assert(component.request_scan(1));
  ++scenarios;
  std::cout << "PASS " << scenarios
            << " read-only diagnostics scenarios (policy, reader, production hub/wrapper, HTTP)\n";
}
