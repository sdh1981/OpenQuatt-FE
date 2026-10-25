#pragma once

#ifdef NDEBUG
#error "Offline tests require assertions; compile with -UNDEBUG"
#endif

#include <algorithm>
#include <array>
#include <cassert>
#include <deque>
#include <functional>
#include <vector>
#include "components/openquatt_odu_firmware/protocol.h"

using namespace oq_odu_firmware;

// Independent device-side wire/flash model. Production codecs are not used to
// parse requests, generate replies or decide which block the bootloader accepts.
struct Device : Bus {
  std::vector<uint8_t> image, programmed;
  std::deque<uint8_t> rx;
  std::array<uint8_t, SERIAL_SIZE> serial{};
  Session durable{};
  bool have_durable{false};
  uint16_t version{0x0200}, board{TARGET_BOARD}, mode{0}, hz{0}, last{0}, boot_state{3};
  unsigned writes{0}, frames{0}, block_sends{0}, releases{0}, setup{0};
  bool lease{false}, drain{true}, busy{false}, reboot{true}, drop_last{false}, boot{false};
  bool corrupt_once{false}, wrong_slave_once{false}, nack_once{false}, stale_once{false}, silence{false};
  bool future{false}, wrong_board_after{false}, boot_ready{true}, wrong_echo{false}, exception{false};
  bool wrong_serial_after{false}, heartbeat{false};
  unsigned fragment{256}, budget{256};
  unsigned drop_ack_at{0};
  uint32_t last_heartbeat{0};
  bool pending{false}, mark_fails{false}, clear_fails{false}, release_fails{false};
  std::function<void(unsigned)> on_accepted;
  std::function<void(const uint8_t*, size_t)> before_send;

  explicit Device(std::vector<uint8_t> data) : image(std::move(data)), programmed(image.size(), 0xFF) {
    memcpy(serial.data(), "SIM-ODU-0000001", 15);
  }
  bool acquire() override {
    if (busy) return false;
    lease = true;
    return true;
  }
  bool ready() override { return drain; }
  bool release() override {
    if (release_fails) return false;
    lease = false;
    ++releases;
    rx.clear();
    return true;
  }
  void seal(const Session& session) {
    const auto generation = have_durable ? durable.generation + 1 : 1;
    durable = session;
    durable.generation = generation;
    durable.crc = checksum(&durable, offsetof(Session, crc));
    assert(valid_session(durable));
    have_durable = true;
  }
  bool checkpoint(const Session& session) override {
    if (mark_fails) return false;
    seal(session);
    pending = true;
    return true;
  }
  bool complete(const Session& session) override {
    seal(session);
    if (clear_fails) return false;
    pending = false;
    return true;
  }
  bool read(uint8_t& byte) override {
    if (rx.empty() || budget == 0) return false;
    --budget;
    byte = rx.front();
    rx.pop_front();
    return true;
  }
  static unsigned reference_crc(const uint8_t* bytes, size_t count) {
    unsigned crc = 65535;
    for (size_t i = 0; i < count; ++i) {
      crc ^= bytes[i];
      for (unsigned bit = 0; bit < 8; ++bit) crc = crc & 1 ? (crc / 2) ^ 40961 : crc / 2;
    }
    return crc;
  }
  void reply(std::vector<uint8_t> data) {
    const auto crc = reference_crc(data.data(), data.size());
    data.push_back(crc & 255);
    data.push_back(crc >> 8);
    if (corrupt_once) {
      corrupt_once = false;
      data.back() ^= 128;
    }
    if (wrong_slave_once) {
      wrong_slave_once = false;
      data[0] ^= 4;
    }
    rx.insert(rx.end(), data.begin(), data.end());
  }
  void burn_reply(uint8_t slave, uint16_t block, bool ack, bool complete = false) {
    reply({slave, 21, static_cast<uint8_t>(block >> 8), static_cast<uint8_t>(block),
           static_cast<uint8_t>(complete ? 9 : 0), static_cast<uint8_t>(ack ? 3 : 0), 128});
  }
  void advance(uint32_t now) {
    budget = fragment;
    if (heartbeat && boot && boot_state == 3 && now - last_heartbeat >= 3000) {
      last_heartbeat = now;
      burn_reply(have_durable ? durable.slave : 1, last, true);
    }
  }
  void restart_controller() {
    lease = false;
    rx.clear();
  }
  void restart_odu() {
    boot = true;
    boot_state = 0;
    last = 0;
    setup = 0;
    rx.clear();
  }

  bool send(const uint8_t* data, size_t size) override {
    assert(lease && drain && size >= 5 && reference_crc(data, size) == 0);
    if (before_send) before_send(data, size);
    ++frames;
    if (silence) return true;
    const auto slave = data[0];
    const unsigned address = (data[2] << 8) | data[3];
    if (exception) {
      reply({slave, static_cast<uint8_t>(data[1] | 128), 2});
      return true;
    }
    if (data[1] == 3) {
      if (boot && address != 10000) return true;
      if (!boot && address == 10000) return true;
      if (address == 11219) {
        assert(data[4] == 0 && data[5] == 20);
        std::vector<uint8_t> bytes{slave, 3, 40};
        bytes.insert(bytes.end(), serial.begin(), serial.end());
        if (wrong_serial_after && last == BLOCKS) bytes[3] ^= 1;
        reply(bytes);
        return true;
      }
      assert(data[4] == 0 && data[5] == 1);
      uint16_t value = 0;
      switch (address) {
        case 2122:
          value = version;
          break;
        case 2127:
          value = wrong_board_after && last == BLOCKS ? 0x0E37 : board;
          break;
        case 2099:
          value = mode;
          break;
        case 2103:
          value = hz;
          break;
        case 10000:
          value = boot_ready ? boot_state : 2;
          break;
        default:
          assert(false);
      }
      reply({slave, 3, 2, static_cast<uint8_t>(value >> 8), static_cast<uint8_t>(value)});
    } else if (data[1] == 6) {
      assert(pending && have_durable && valid_session(durable));
      ++writes;
      const unsigned value = (data[4] << 8) | data[5];
      if (address == 3999 && value == 7) {
        setup = 1;
      } else if (address == 4004 && value == 0x5AA5 && setup >= 1) {
        setup = 2;
      } else {
        assert(address == 9999 && value == 1 && (setup >= 2 || boot));
        setup = 3;
        boot = true;
        boot_state = 3;
        last = 0;
        std::fill(programmed.begin(), programmed.end(), 0xFF);
      }
      std::vector<uint8_t> echo(data, data + 6);
      if (wrong_echo) ++echo[3];
      reply(echo);
    } else {
      assert(data[1] == 21 && size == 137);
      if (!boot || boot_state != 3) return true;
      ++block_sends;
      const unsigned block = data[2] | (data[3] << 8);
      assert(block > 0 && block <= 912 && data[4] == (block == 912) && data[5] == 0 && data[6] == 128);
      assert(have_durable && block <= durable.authorized);
      assert(std::equal(data + 7, data + 135, image.data() + (block - 1) * 128));
      if (nack_once) {
        nack_once = false;
        burn_reply(slave, block - 1, false);
        return true;
      }
      if (stale_once) {
        stale_once = false;
        burn_reply(slave, block - 1, true);
        return true;
      }
      if (block != last + 1) {
        burn_reply(slave, last, false);
        return true;
      }
      std::copy(data + 7, data + 135, programmed.begin() + (block - 1) * 128);
      last = block;
      if (last == 912 && reboot) {
        assert(programmed == image);
        version = TARGET_VERSION;
        boot = false;
      }
      if (on_accepted) on_accepted(block);
      if ((last == 912 && drop_last) || drop_ack_at == block) {
        drop_ack_at = 0;
        return true;
      }
      burn_reply(slave, future ? block + 1 : block, true, block == 912);
    }
    return true;
  }
};
