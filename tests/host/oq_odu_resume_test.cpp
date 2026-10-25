#include <cassert>
#include <iostream>
#include <memory>
#include "tests/odu_firmware_bus/model.h"
#include "tests/odu_firmware_bus/flash_model.h"

struct PowerCut {};

struct PersistentDevice : Device {
  FlashMemory flash;
  Storage journal{flash};
  explicit PersistentDevice(const std::vector<uint8_t>& image) : Device(image) {
    assert(journal.begin_stage());
    while (journal.staging()) journal.step(image.data());
    assert(journal.has_image());
  }
  bool checkpoint(const Session& session) override {
    if (mark_fails || !journal.save_session(session)) return false;
    assert(journal.load_session(durable) == Storage::Journal::VALID);
    have_durable = pending = true;
    return true;
  }
  bool complete(const Session& session) override {
    if (!journal.save_session(session)) return false;
    assert(journal.load_session(durable) == Storage::Journal::VALID);
    have_durable = true;
    if (clear_fails) return false;
    pending = false;
    return true;
  }
  Session recover() {
    Storage rebooted(flash);
    Session saved;
    assert(rebooted.load_session(saved) == Storage::Journal::VALID);
    durable = saved;
    have_durable = true;
    return saved;
  }
};

void drive(Engine& engine, Device& device, uint32_t& now, uint32_t limit = 300000) {
  const uint32_t end = now + limit;
  while (engine.active() && static_cast<int32_t>(now - end) < 0) {
    device.advance(now);
    engine.tick(now, true);
    now += 20;
  }
}

int main() {
  std::vector<uint8_t> image(IMAGE_SIZE, 0x5A);
  image[0] = 0x82;
  const auto crc = xmodem(image.data(), image.size() - 2);
  image[image.size() - 2] = crc >> 8;
  image.back() = crc & 255;
  unsigned cases = 0;
  // Every accepted block, including 32/256-sector boundaries and the final
  // block before its ACK: a new controller instance must query the real ODU.
  for (unsigned block = 1; block <= BLOCKS; ++block) {
    PersistentDevice device(image);
    uint32_t now = 0;
    auto engine = std::make_unique<Engine>(device);
    device.on_accepted = [block](unsigned actual) {
      if (actual == block) throw PowerCut{};
    };
    assert(engine->start(image.data(), image.size(), 1, true, true, now));
    try {
      drive(*engine, device, now);
      assert(false);
    } catch (const PowerCut&) {
    }
    assert(device.last == block && device.pending);
    const unsigned writes = device.writes;
    const auto saved = device.recover();
    device.on_accepted = {};
    device.restart_controller();
    engine = std::make_unique<Engine>(device);
    engine->restore_pending(&saved);
    assert(!engine->resume(image.data(), image.size(), saved, false, true, now));
    assert(!engine->resume(image.data(), image.size(), saved, true, false, now));
    assert(engine->resume(image.data(), image.size(), saved, true, true, now));
    drive(*engine, device, now);
    if (engine->state() != State::COMPLETE) {
      std::cerr << "block " << block << " " << state_name(engine->state()) << " " << engine->error() << '\n';
      return 1;
    }
    assert(device.programmed == image && !device.pending && !engine->locked());
    assert(device.writes == writes);  // No reactivation/erase when progress survived.
    assert(device.durable.complete == 1 && device.durable.recoveries == 1);
    device.flash.assert_archives_untouched();
    ++cases;
  }
  for (unsigned kind = 0; kind < 11; ++kind) {
    PersistentDevice device(image);
    uint32_t now = 0;
    Engine first(device);
    device.on_accepted = [](unsigned block) {
      if (block == 100) throw PowerCut{};
    };
    assert(first.start(image.data(), image.size(), 1, true, true, now));
    try {
      drive(first, device, now);
      assert(false);
    } catch (const PowerCut&) {
    }
    device.on_accepted = {};
    device.restart_controller();
    auto saved = device.recover();
    if (kind == 0) device.restart_odu();  // Both controllers lost power: sequence has reset to zero.
    if (kind == 1) {
      device.boot = false;
      device.mode = 0;
      device.setup = 0;
    }  // Old app still running.
    if (kind == 2) {
      device.heartbeat = true;
      device.last_heartbeat = now - 3000;
    }
    if (kind == 3) {
      device.fragment = 1;
      device.corrupt_once = true;
    }
    if (kind == 4) device.drop_last = true;
    if (kind == 5) {
      device.boot = false;
      device.serial[0] ^= 1;
    }
    if (kind == 6) {
      device.boot = false;
      device.board ^= 1;
    }
    if (kind == 7) {
      device.boot = false;
      device.version = 0x0300;
    }
    if (kind == 8) {
      device.boot = false;
      device.hz = 20;
    }
    if (kind == 9) saved.recoveries = MAX_RECOVERIES;
    if (kind == 10) {
      saved.image_sha256[0] ^= 1;
    }
    saved.crc = checksum(&saved, offsetof(Session, crc));
    Engine recovered(device);
    recovered.restore_pending(&saved);
    const bool resumed = recovered.resume(image.data(), image.size(), saved, true, true, now);
    const auto writes = device.writes;
    if (kind >= 9) {
      assert(!resumed && recovered.locked());
    } else {
      assert(resumed);
      drive(recovered, device, now);
      if (kind <= 4)
        assert(recovered.state() == State::COMPLETE && device.programmed == image);
      else
        assert(recovered.state() == State::RECOVERY_REQUIRED && recovered.locked() && device.writes == writes);
    }
    ++cases;
  }
  // Intent was saved, but power failed before the corresponding wire frame.
  for (unsigned cutoff : {0U, 1U, 32U, 912U}) {
    PersistentDevice device(image);
    uint32_t now = 0;
    Engine first(device);
    device.before_send = [cutoff](const uint8_t* bytes, size_t) {
      if ((cutoff == 0 && bytes[1] == 6) ||
          (cutoff > 0 && bytes[1] == 21 && static_cast<unsigned>(bytes[2] | (bytes[3] << 8)) == cutoff))
        throw PowerCut{};
    };
    assert(first.start(image.data(), image.size(), 1, true, true, now));
    try {
      drive(first, device, now);
      assert(false);
    } catch (const PowerCut&) {
    }
    device.before_send = {};
    device.restart_controller();
    const auto saved = device.recover();
    Engine recovered(device);
    recovered.restore_pending(&saved);
    assert(recovered.resume(image.data(), image.size(), saved, true, true, now));
    drive(recovered, device, now);
    assert(recovered.state() == State::COMPLETE && device.programmed == image);
    ++cases;
  }
  {
    PersistentDevice device(image);
    uint32_t now = 0;
    Engine engine(device);
    device.drop_ack_at = 50;
    assert(engine.start(image.data(), image.size(), 1, true, true, now));
    drive(engine, device, now);
    assert(engine.state() == State::COMPLETE && device.programmed == image);
    ++cases;
  }
  {
    PersistentDevice device(image);
    uint32_t now = 0;
    Engine first(device);
    device.clear_fails = true;
    assert(first.start(image.data(), image.size(), 1, true, true, now));
    drive(first, device, now);
    assert(first.state() == State::RECOVERY_REQUIRED && device.pending && device.durable.complete == 1);
    const auto saved = device.recover();
    const unsigned sends = device.block_sends;
    device.clear_fails = false;
    device.restart_controller();
    Engine recovered(device);
    recovered.restore_pending(&saved);
    assert(recovered.resume(image.data(), image.size(), saved, true, true, now));
    drive(recovered, device, now);
    assert(recovered.state() == State::COMPLETE && !device.pending && device.block_sends == sends);
    ++cases;
  }
  {
    PersistentDevice device(image);
    Engine engine(device);
    engine.restore_pending();
    assert(!engine.retryable() && engine.locked() && device.frames == 0);
    ++cases;
  }
  {
    PersistentDevice device(image);
    uint32_t now = 0;
    Engine first(device);
    device.on_accepted = [](unsigned block) {
      if (block == 100) throw PowerCut{};
    };
    assert(first.start(image.data(), image.size(), 1, true, true, now));
    try {
      drive(first, device, now);
      assert(false);
    } catch (const PowerCut&) {
    }
    device.on_accepted = {};
    device.restart_controller();
    const auto saved = device.recover();
    device.last = 101;  // Neither the journal nor any transmitted frame authorized this position.
    Engine recovered(device);
    recovered.restore_pending(&saved);
    assert(recovered.resume(image.data(), image.size(), saved, true, true, now));
    const auto writes = device.writes;
    drive(recovered, device, now);
    assert(recovered.state() == State::RECOVERY_REQUIRED && !recovered.retryable());
    assert(device.writes == writes && device.last == 101);
    ++cases;
  }
  std::cout << "PASS " << cases << " recovery scenarios, including controller loss after every one of 912 blocks\n";
}
