#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <span>
#include <vector>

// UART/queue test double only. The production OpenQuatt hub .cpp is compiled
// unchanged against this dependency, to test lease ownership and dispatch.
namespace esphome {
struct LogString {};
struct GPIOPin {
  bool level{false};
  void digital_write(bool value) { level = value; }
};
namespace modbus {
enum class FrameState { READY, WAITING, RETIRED };
struct Frame {
  uint8_t address() const { return 1; }
  std::span<const uint8_t> pdu() const { return {}; }
};
struct Command {
  Frame frame;
  FrameState state{FrameState::READY};
  std::function<void()> cancelled;
  void retire() {
    if (state != FrameState::WAITING) state = FrameState::RETIRED;
  }
};
struct UART {
  uint32_t get_baud_rate() const { return 19200; }
};
class Modbus {
 public:
  virtual ~Modbus() = default;
  virtual void loop() {}
  virtual bool tx_blocked() { return line_busy || !input.empty() || !rx_buffer_.empty(); }
  size_t available() const { return input.size(); }
  bool read_byte(uint8_t* value) {
    if (input.empty()) return false;
    *value = input.front();
    input.pop_front();
    return true;
  }
  void write_array(const uint8_t* data, size_t size) { output.insert(output.end(), data, data + size); }
  void flush() { ++flushes; }
  bool line_busy{false};
  int32_t delay_us{0};
  unsigned flushes{0};
  std::deque<uint8_t> input;
  std::vector<uint8_t> output;

 protected:
  virtual void parse_modbus_frames() = 0;
  virtual int32_t tx_delay_remaining() { return delay_us; }
  bool parse_modbus_server_frame_() {
    rx_buffer_.clear();
    return true;
  }
  void clear_rx_buffer_(const LogString*, bool) { rx_buffer_.clear(); }
  bool timeout_() { return false; }
  uint32_t last_send_{0}, last_modbus_byte_{0}, last_send_tx_offset_{0};
  uint8_t bits_per_char_{11};
  GPIOPin* flow_control_pin_{nullptr};
  UART uart_;
  UART* parent_{&uart_};
  std::vector<uint8_t> rx_buffer_;
};
class ModbusClientHub : public Modbus {
 public:
  void loop() override {
    ++normal_loops;
    if (waiting_for_response_ && response_finished) {
      waiting_for_response_ = false;
      for (auto& command : tx_buffer_)
        if (command.state == FrameState::WAITING) command.state = FrameState::RETIRED;
      sweep_();
    }
    parse_modbus_frames();
    if (!tx_blocked() && !tx_buffer_.empty()) ++ordinary_sends;
  }
  bool tx_blocked() override { return waiting_for_response_ || Modbus::tx_blocked(); }
  void queue(std::function<void()> callback = {}) { tx_buffer_.push_back({{}, FrameState::READY, callback}); }
  void in_flight() {
    tx_buffer_.push_back({{}, FrameState::WAITING, {}});
    waiting_for_response_ = true;
  }
  bool response_finished{false};
  unsigned ordinary_sends{0}, normal_loops{0};

 protected:
  void parse_modbus_frames() override {}
  Command* find_waiting_() {
    for (auto& command : tx_buffer_)
      if (command.state == FrameState::WAITING) return &command;
    return nullptr;
  }
  void sweep_() {
    // Match upstream callback reentrancy: callbacks run before a later sweep
    // observes commands enqueued during delivery.
    std::vector<std::function<void()>> callbacks;
    for (auto it = tx_buffer_.begin(); it != tx_buffer_.end();) {
      if (it->state == FrameState::RETIRED) {
        callbacks.push_back(it->cancelled);
        it = tx_buffer_.erase(it);
      } else
        ++it;
    }
    for (auto& callback : callbacks)
      if (callback) callback();
  }
  std::deque<Command> tx_buffer_;
  bool waiting_for_response_{false}, sweep_needed_{false};
};
}  // namespace modbus
}  // namespace esphome
