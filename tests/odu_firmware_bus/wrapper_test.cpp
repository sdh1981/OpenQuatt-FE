#include <fstream>
#include <iostream>
#include <memory>
#include "components/openquatt_odu_firmware/OpenQuattOduFirmware.h"

using Module = esphome::openquatt_odu_firmware::OpenQuattOduFirmware;
struct PowerCut {};

struct Fixture {
  Device device;
  FlashMemory flash;
  esphome::openquatt_modbus::OpenQuattModbusClientHub hub;
  esphome::openquatt_web_auth::OpenQuattWebAuth auth;
  AsyncWebServerRequest request;
  explicit Fixture(const std::vector<uint8_t>& image) : device(image) {
    harness::device = &device;
    harness::flash = &flash;
    harness::reference_image = image;
    harness::nvs_exists = harness::nvs_staged = harness::nvs_save_fails = harness::nvs_sync_fails = false;
    harness::psram_fails = harness::sha_fails = false;
    harness::authenticated = harness::guard = true;
    harness::auth_configured = true;
    harness::nvs_value = harness::nvs_candidate = harness::absolute_ms = 0;
  }
  std::unique_ptr<Module> boot(bool writes = true, bool duo = false, unsigned address = 1, bool automatic = true) {
    hub.owner = nullptr;
    device.restart_controller();
    harness::nvs_staged = false;
    harness::now = 0;
    harness::enabled = true;
    auto module = std::make_unique<Module>();
    module->set_hub(&hub);
    module->set_web_auth(&auth);
    module->set_allow_writes(writes);
    module->set_duo(duo);
    module->set_auto_resume(automatic);
    module->set_hp1_address(address);
    module->set_hold_control([] {
      harness::enabled = false;
      ++harness::hold_calls;
    });
    module->set_safe_to_start([] { return !harness::enabled && harness::guard; });
    module->set_safe_to_continue([] { return !harness::enabled && harness::guard; });
    module->setup();
    return module;
  }
  void step(Module& module) {
    harness::now += 20;
    harness::absolute_ms += 20;
    device.advance(harness::absolute_ms);
    module.loop();
  }
  std::string status(Module& module) {
    httpd_req_t req;
    module.write_status(&req);
    return req.response;
  }
  bool has(Module& module, const char* text) { return status(module).find(text) != std::string::npos; }
  void until(Module& module, const char* text, unsigned milliseconds = 250000) {
    for (unsigned i = 0; i < milliseconds / 20 && !has(module, text); ++i) step(module);
    if (!has(module, text)) {
      std::cerr << "Missing " << text << " in " << status(module) << '\n';
      assert(false);
    }
  }
  void elapsed(Module& module, unsigned milliseconds) {
    for (unsigned i = 0; i < milliseconds / 20; ++i) step(module);
  }
  void upload(Module& module, const std::vector<uint8_t>& image) {
    module.upload(&request, image.data(), image.size(), 0, image.size());
  }
  void start(Module& module, const std::vector<uint8_t>& image, unsigned hp = 1) {
    harness::enabled = false;
    upload(module, image);
    assert(module.request_start(hp));
  }
};

int main(int argc, char** argv) {
  assert(argc == 2);
  std::ifstream file(argv[1], std::ios::binary);
  std::vector<uint8_t> image{std::istreambuf_iterator<char>(file), {}};
  assert(valid_image(image.data(), image.size()));
  unsigned cases = 0;
  {
    Fixture fixture(image);
    auto module = fixture.boot();
    harness::auth_configured = harness::authenticated = false;
    auto& handler = *esphome::web_server_base::server.handler;
    auto& request = fixture.request;
    for (const char* route : {"/openquatt/odu-firmware", "/openquatt/odu-firmware/status"}) {
      request.path = route;
      request.raw = {};
      assert(handler.canHandle(&request));
      handler.handleRequest(&request);
      assert(request.raw.status != "401" && !request.raw.response.empty());
    }
    assert(module->permitted(&request));
    request.path = "/openquatt/odu-firmware/start";
    request.verb = HTTP_POST;
    request.raw = {};
    handler.handleRequest(&request);
    assert(request.raw.status == "409");  // No image: reject start, without a login challenge.
    assert(fixture.device.frames == 0 && fixture.flash.writes == 0);
    ++cases;
  }
  {
    Fixture fixture(image);
    auto module = fixture.boot();
    harness::auth_configured = true;
    harness::authenticated = false;
    auto& handler = *esphome::web_server_base::server.handler;
    auto& request = fixture.request;
    for (const char* route : {"/openquatt/odu-firmware/status", "/openquatt/odu-firmware/start"}) {
      request.path = route;
      request.verb = strcmp(route, "/openquatt/odu-firmware/start") == 0 ? HTTP_POST : HTTP_GET;
      request.raw = {};
      handler.handleRequest(&request);
      assert(request.raw.status == "401");
      assert(!module->permitted(&request));
    }
    harness::authenticated = true;
    request.path = "/openquatt/odu-firmware/status";
    request.verb = HTTP_GET;
    request.raw = {};
    handler.handleRequest(&request);
    assert(request.raw.status != "401" && !request.raw.response.empty());
    assert(fixture.device.frames == 0);
    ++cases;
  }
  for (bool wrong_origin : {false, true}) {
    Fixture fixture(image);
    auto module = fixture.boot();
    harness::auth_configured = harness::authenticated = false;
    auto& request = fixture.request;
    request.path = "/openquatt/odu-firmware/image";
    request.verb = HTTP_POST;
    request.headers[wrong_origin ? "Origin" : "X-OQ-CSRF"] = wrong_origin ? "http://foreign.test" : "bad-token";
    auto& handler = *esphome::web_server_base::server.handler;
    handler.handleBody(&request, image.data(), image.size(), 0, image.size());
    handler.handleRequest(&request);
    assert(request.raw.status == "403 Forbidden");
    assert(fixture.has(*module, "\"received\":0"));
    assert(fixture.device.frames == 0 && fixture.flash.writes == 0);
    ++cases;
  }
  {
    Fixture fixture(image);
    auto module = fixture.boot();
    harness::auth_configured = harness::authenticated = false;
    auto& request = fixture.request;
    request.path = "/openquatt/odu-firmware/image";
    request.verb = HTTP_POST;
    auto& handler = *esphome::web_server_base::server.handler;
    handler.handleBody(&request, image.data(), image.size(), 0, image.size());
    handler.handleRequest(&request);
    assert(request.raw.status != "401" && fixture.has(*module, "IMAGE_VERIFIED"));
    fixture.elapsed(*module, 10000);
    assert(fixture.device.frames == 0 && fixture.flash.writes == 0 && fixture.flash.erases == 0);
    ++cases;
  }
  for (unsigned kind = 0; kind < 13; ++kind) {
    Fixture fixture(image);
    const bool duo = kind == 10;
    auto module = fixture.boot(true, duo);
    fixture.device.on_accepted = [kind](unsigned block) {
      if (block == (kind == 2 ? 912U : 100U)) throw PowerCut{};
    };
    if (kind == 3) {
      fixture.device.on_accepted = {};
      fixture.device.before_send = [](const uint8_t* data, size_t) {
        if (data[1] == 6) throw PowerCut{};
      };
    }
    fixture.start(*module, image, duo ? 2 : 1);
    try {
      fixture.elapsed(*module, 250000);
      assert(false);
    } catch (const PowerCut&) {
    }
    fixture.device.on_accepted = {};
    fixture.device.before_send = {};
    module.reset();
    const unsigned frames = fixture.device.frames, writes = fixture.device.writes, sends = fixture.device.block_sends;
    assert(harness::nvs_value != 0);
    if (kind == 1) fixture.device.restart_odu();
    if (kind == 4) fixture.flash.bytes[Storage::Layout::ODU_UPDATE_IMAGE_OFFSET + 100] ^= 1;
    if (kind == 7) {
      fixture.device.boot = false;
      fixture.device.serial[0] ^= 1;
    }
    if (kind == 8) harness::psram_fails = true;
    if (kind == 9) {
      Storage storage(fixture.flash);
      Session saved;
      assert(storage.load_session(saved) == Storage::Journal::VALID);
      saved.recoveries = MAX_RECOVERIES;
      assert(storage.save_session(saved));
    }
    if (kind == 11) {
      assert(fixture.flash.erase(Storage::Layout::ODU_UPDATE_HEADER_OFFSET, Storage::Layout::SECTOR_SIZE));
    }
    module = fixture.boot(kind != 5, duo, kind == 6 ? 2 : 1, kind != 12);
    assert(fixture.hub.owner != nullptr && fixture.device.frames == frames);
    if (kind == 4 || kind == 8 || kind == 11) {
      fixture.elapsed(*module, 10000);
      assert(fixture.device.frames == frames && !harness::enabled);
      assert(fixture.has(*module, "\"can_upload\":true"));
      harness::psram_fails = false;
      auto bad = image;
      bad[100] ^= 1;
      const auto crc = xmodem(bad.data(), bad.size() - 2);
      bad[bad.size() - 2] = crc >> 8;
      bad.back() = crc & 255;
      const unsigned flash_writes = fixture.flash.writes;
      fixture.upload(*module, bad);
      fixture.elapsed(*module, 2000);
      assert(fixture.has(*module, "INVALID_IMAGE") && fixture.device.frames == frames);
      assert(fixture.flash.writes == flash_writes);
      fixture.upload(*module, image);  // No start/resume call: matching cache repair resumes on its own.
    }
    if (kind == 5 || kind == 6 || kind == 9 || kind == 12) {
      fixture.elapsed(*module, 15000);
      assert(fixture.device.frames == frames && fixture.hub.owner != nullptr && !harness::enabled);
      if (kind != 5) {
        ++cases;
        continue;
      }
      module->set_allow_writes(true);  // Only the in-memory test instance; no device configuration changes.
    }
    if (kind == 7) {
      fixture.until(*module, "serial_changed");
      assert(fixture.device.writes == writes && fixture.hub.owner != nullptr);
      ++cases;
      continue;
    }
    fixture.until(*module, "\"state\":\"COMPLETE\"");
    assert(fixture.device.programmed == image && harness::nvs_value == 0 && fixture.hub.owner == nullptr);
    assert(!harness::enabled);
    if (kind == 0 || kind == 2 || kind == 10) assert(fixture.device.writes == writes);
    if (kind == 2) assert(fixture.device.block_sends == sends);
    if (kind == 10) assert(fixture.has(*module, "\"hp\":2"));
    Storage storage(fixture.flash);
    Session saved;
    assert(storage.load_session(saved) == Storage::Journal::VALID && saved.complete == 1 && saved.recoveries == 1);
    fixture.flash.assert_archives_untouched();
    ++cases;
  }
  {
    Fixture fixture(image);
    auto module = fixture.boot(false);
    harness::enabled = false;
    fixture.upload(*module, image);
    assert(!module->request_start(1));
    fixture.elapsed(*module, 10000);
    assert(fixture.device.frames == 0 && fixture.flash.writes == 0 && fixture.flash.erases == 0);
    ++cases;
  }
  {
    Fixture fixture(image);
    harness::nvs_exists = true;
    harness::nvs_value = 0x4F514655U;
    auto module = fixture.boot();
    fixture.elapsed(*module, 10000);
    assert(fixture.device.frames == 0 && fixture.hub.owner != nullptr && harness::nvs_value != 0);
    assert(fixture.has(*module, "legacy_or_invalid_session"));
    ++cases;
  }
  {
    Fixture fixture(image);
    auto module = fixture.boot();
    fixture.device.on_accepted = [](unsigned block) {
      if (block == 100) harness::device->silence = true;
    };
    fixture.start(*module, image);
    fixture.until(*module, "\"state\":\"RECOVERY_REQUIRED\"");
    const unsigned writes = fixture.device.writes;
    fixture.device.on_accepted = {};
    fixture.device.silence = false;
    fixture.until(*module, "\"state\":\"COMPLETE\"");
    assert(fixture.device.writes == writes && fixture.device.programmed == image && harness::nvs_value == 0);
    ++cases;
  }
  {
    Fixture fixture(image);
    auto module = fixture.boot();
    harness::nvs_sync_fails = true;
    fixture.start(*module, image);
    fixture.until(*module, "\"state\":\"RECOVERY_REQUIRED\"");
    assert(fixture.device.writes == 0 && harness::nvs_value == 0);
    module.reset();
    harness::nvs_sync_fails = false;
    module = fixture.boot();
    fixture.until(*module, "\"state\":\"COMPLETE\"");
    assert(fixture.device.programmed == image && harness::nvs_value == 0);
    ++cases;
  }
  {
    Fixture fixture(image);
    auto module = fixture.boot();
    fixture.device.on_accepted = [](unsigned block) {
      if (block == 912) harness::nvs_sync_fails = true;
    };
    fixture.start(*module, image);
    fixture.until(*module, "checkpoint_clear_failed");
    const unsigned sends = fixture.device.block_sends;
    module.reset();
    fixture.device.on_accepted = {};
    harness::nvs_sync_fails = false;
    module = fixture.boot();
    fixture.until(*module, "\"state\":\"COMPLETE\"");
    assert(harness::nvs_value == 0 && fixture.device.block_sends == sends);
    ++cases;
  }
  {
    Fixture fixture(image);
    auto module = fixture.boot();
    harness::nvs_sync_fails = true;
    fixture.start(*module, image);
    fixture.until(*module, "AUTO_RESUME_LIMIT", 100000);
    const auto writes = fixture.flash.writes;
    const auto frames = fixture.device.frames;
    fixture.elapsed(*module, 60000);
    assert(fixture.flash.writes == writes && fixture.device.frames == frames && fixture.device.writes == 0);
    assert(fixture.hub.owner != nullptr);
    ++cases;
  }
  for (bool erase : {false, true}) {
    Fixture fixture(image);
    auto module = fixture.boot();
    fixture.device.on_accepted = [erase](unsigned block) {
      if (block == (erase ? 31 : 32)) {
        if (erase)
          harness::flash->erase_budget = 1024;
        else
          harness::flash->write_budget = 63;
      }
    };
    fixture.start(*module, image);
    fixture.until(*module, "checkpoint_failed");
    const auto writes = fixture.device.writes;
    module.reset();
    fixture.device.on_accepted = {};
    fixture.flash.write_budget = fixture.flash.erase_budget = -1;
    module = fixture.boot();
    fixture.until(*module, "\"state\":\"COMPLETE\"");
    assert(fixture.device.writes == writes && fixture.device.programmed == image);
    fixture.flash.assert_archives_untouched();
    ++cases;
  }
  {
    // A previous completed HP2 session must never clear HP1's newer marker,
    // even if every record of that newer session becomes unreadable.
    Fixture fixture(image);
    auto module = fixture.boot(true, true);
    fixture.start(*module, image, 2);
    fixture.until(*module, "\"state\":\"COMPLETE\"");
    fixture.device.version = 0x0200;
    fixture.device.last = 0;
    fixture.device.before_send = [](const uint8_t* data, size_t) {
      if (data[1] == 6) throw PowerCut{};
    };
    fixture.start(*module, image, 1);
    try {
      fixture.elapsed(*module, 250000);
      assert(false);
    } catch (const PowerCut&) {
    }
    const auto active_id = harness::nvs_value;
    assert(active_id != 0);
    for (unsigned slot = 0; slot < 64; ++slot) {
      const auto offset = Storage::Layout::ODU_UPDATE_OFFSET + slot * sizeof(Session);
      Session value;
      assert(fixture.flash.read(offset, &value, sizeof(value)));
      if (valid_session(value) && value.session_id == active_id)
        fixture.flash.bytes[offset + offsetof(Session, crc)] ^= 1;
    }
    Storage storage(fixture.flash);
    Session previous;
    assert(storage.load_session(previous) == Storage::Journal::VALID && previous.complete && previous.hp == 2);
    assert(previous.session_id != active_id);
    module.reset();
    fixture.device.before_send = {};
    fixture.device.version = TARGET_VERSION;
    const auto frames = fixture.device.frames;
    module = fixture.boot(true, true);
    fixture.elapsed(*module, 15000);
    assert(fixture.device.frames == frames && fixture.hub.owner != nullptr && harness::nvs_value == active_id);
    ++cases;
  }
  std::cout << "PASS " << cases << " actual ESPHome-wrapper recovery scenarios (mock SDK, flash, UART and crypto)\n";
}
