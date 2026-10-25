#include <algorithm>
#include <cassert>
#include <deque>
#include <fstream>
#include <iostream>
#include <vector>
#include "components/openquatt_odu_firmware/protocol.h"

using namespace oq_odu_firmware;

#include "tests/odu_firmware_bus/model.h"

void run(Engine& engine, Device& device, uint32_t start = 0, uint32_t duration = 300000) {
  for (uint32_t elapsed = 0; elapsed < duration && engine.active(); elapsed += 20) {
    device.advance(start + elapsed);
    engine.tick(start + elapsed, true);
  }
}

int main(int argc, char** argv) {
  assert(argc <= 2);
  std::vector<uint8_t> image(IMAGE_SIZE, 0x55);
  if (argc == 2) {
    std::ifstream file(argv[1], std::ios::binary);
    image.assign(std::istreambuf_iterator<char>(file), {});
  } else {
    // The no-argument host regression runner needs no private binary fixture.
    image[0] = 0x82;
    const auto crc = xmodem(image.data(), image.size() - 2);
    image[image.size() - 2] = crc >> 8;
    image.back() = crc & 255;
  }
  assert(valid_image(image.data(), image.size()));
  uint8_t frame[FRAME_SIZE];
  assert(block_frame(frame, 1, 1, image.data()) == 137);
  assert(frame[2] == 1 && frame[3] == 0);
  if (argc == 2) assert(frame[135] == 0x5E && frame[136] == 0x4C);
  block_frame(frame, 1, BLOCKS, image.data());
  assert(frame[2] == 0x90 && frame[3] == 3 && frame[4] == 1);
  if (argc == 2) assert(frame[135] == 0x6C && frame[136] == 0x2F);
  assert(block_frame(frame, 0, 1, image.data()) == 0);
  assert(block_frame(frame, 1, 913, image.data()) == 0);
  auto bad = image;
  bad[100] ^= 1;
  assert(!valid_image(bad.data(), bad.size()));
  assert(!valid_image(image.data(), image.size() - 1));
  unsigned cases = 0;
  for (unsigned fault = 0; fault < 17; ++fault) {
    Device device(image);
    if (fault == 1) device.fragment = 1;
    if (fault == 2) device.corrupt_once = true;
    if (fault == 3) device.wrong_slave_once = true;
    if (fault == 4) device.nack_once = true;
    if (fault == 5) device.stale_once = true;
    if (fault == 6) device.drop_last = true;
    if (fault == 7) device.board = 0x0E37;
    if (fault == 8) device.mode = 2;
    if (fault == 9) device.hz = 30;
    if (fault == 10) device.version = TARGET_VERSION;
    if (fault == 11) device.version = 0x0300;
    if (fault == 12) device.future = true;
    if (fault == 13) device.reboot = false;
    if (fault == 14) device.wrong_board_after = true;
    if (fault == 15) device.boot_ready = false;
    if (fault == 16) device.exception = true;
    Engine engine(device);
    assert(engine.start(image.data(), image.size(), 1, true, true, 0));
    run(engine, device);
    const State expected = fault <= 6                   ? State::COMPLETE
                           : fault == 10                ? State::ALREADY_CURRENT
                           : fault >= 12 && fault <= 15 ? State::RECOVERY_REQUIRED
                                                        : State::FAILED;
    if (engine.state() != expected) {
      std::cerr << "case " << fault << ": " << state_name(engine.state()) << ' ' << engine.error() << '\n';
      return 1;
    }
    if (expected == State::COMPLETE) assert(engine.acknowledged() == 912 && device.last == 912);
    if (fault >= 7 && fault <= 11) assert(device.writes == 0);
    assert(engine.locked() == (expected == State::RECOVERY_REQUIRED));
    assert(device.pending == (expected == State::RECOVERY_REQUIRED));
    ++cases;
  }
  {
    Device device(image);
    Engine engine(device);
    assert(!engine.start(image.data(), image.size(), 1, false, true, 0));
    assert(!engine.start(image.data(), image.size(), 1, true, false, 0));
    assert(!engine.start(image.data(), image.size(), 0, true, true, 0));
    assert(!engine.start(bad.data(), bad.size(), 1, true, true, 0));
    assert(device.frames == 0 && !device.lease);
    ++cases;
  }
  {
    Device device(image);
    device.busy = true;
    Engine engine(device);
    assert(!engine.start(image.data(), image.size(), 1, true, true, 0));
    assert(device.frames == 0);
    ++cases;
  }
  {
    Device device(image);
    device.drain = false;
    Engine engine(device);
    assert(engine.start(image.data(), image.size(), 1, true, true, 0));
    run(engine, device);
    assert(engine.state() == State::FAILED && device.frames == 0 && !device.lease);
    ++cases;
  }
  {
    Device device(image);
    Engine engine(device);
    assert(engine.start(image.data(), image.size(), 1, true, true, 0));
    engine.tick(1, false);
    assert(engine.state() == State::FAILED && device.frames == 0 && !device.lease);
    ++cases;
  }
  {
    Device device(image);
    Engine engine(device);
    assert(engine.start(image.data(), image.size(), 2, true, true, 0));
    for (unsigned now = 0; device.writes == 0; now += 20) {
      device.budget = 256;
      engine.tick(now, true);
    }
    engine.tick(10000, false);
    assert(engine.state() == State::RECOVERY_REQUIRED && device.lease);
    assert(!engine.start(image.data(), image.size(), 2, true, true, 10001));
    ++cases;
  }
  {
    Device device(image);
    Engine engine(device);
    const uint32_t now = 0xFFFFF000;
    assert(engine.start(image.data(), image.size(), 2, true, true, now));
    run(engine, device, now);
    assert(engine.state() == State::COMPLETE);
    ++cases;
  }
  {
    Device device(image);
    device.silence = true;
    Engine engine(device);
    assert(engine.start(image.data(), image.size(), 1, true, true, 0));
    run(engine, device);
    assert(engine.state() == State::FAILED && device.frames == 5 && !device.lease);
    ++cases;
  }
  for (unsigned fault = 0; fault < 3; ++fault) {
    Device device(image);
    Engine engine(device);
    if (fault == 0) device.mark_fails = true;
    if (fault == 1) device.clear_fails = true;
    if (fault == 2) device.release_fails = true;
    assert(engine.start(image.data(), image.size(), 1, true, true, 0));
    run(engine, device);
    if (fault == 0)
      assert(engine.state() == State::RECOVERY_REQUIRED && device.writes == 0);
    else
      assert(engine.state() == State::RECOVERY_REQUIRED && engine.locked());
    ++cases;
  }
  {
    Device device(image);
    device.pending = true;
    Engine engine(device);
    engine.restore_pending();
    engine.tick(100, true);
    assert(engine.state() == State::RECOVERY_REQUIRED && engine.locked() && device.frames == 0);
    assert(!engine.start(image.data(), image.size(), 1, true, true, 100));
    ++cases;
  }
  std::cout << "PASS " << cases << " integrated C++ updater scenarios + image/golden frame checks\n";
}
