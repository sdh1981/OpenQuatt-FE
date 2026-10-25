#pragma once

#ifdef NDEBUG
#error "Offline tests require assertions; compile with -UNDEBUG"
#endif

#include <algorithm>
#include <cassert>
#include <vector>
#include "components/openquatt_odu_firmware/storage.h"

struct FlashMemory : oq_odu_firmware::Flash {
  using Layout = oq_odu_firmware::Storage::Layout;
  std::vector<uint8_t> bytes;
  int write_budget{-1}, erase_budget{-1};
  unsigned writes{0}, erases{0};
  bool read_fails{false};
  explicit FlashMemory(size_t size = Layout::ODU_UPDATE_END_OFFSET + Layout::SECTOR_SIZE) : bytes(size, 0xFF) {
    const size_t limit = std::min<size_t>(size, Layout::ODU_UPDATE_OFFSET);
    std::fill(bytes.begin(), bytes.begin() + limit, 0x3C);
  }
  size_t size() const override { return bytes.size(); }
  bool range(size_t offset, size_t count) const {
    assert(offset >= Layout::ODU_UPDATE_OFFSET && offset <= Layout::ODU_UPDATE_END_OFFSET &&
           count <= Layout::ODU_UPDATE_END_OFFSET - offset);
    return offset <= bytes.size() && count <= bytes.size() - offset;
  }
  bool read(size_t offset, void* output, size_t count) override {
    if (!range(offset, count) || read_fails) return false;
    memcpy(output, bytes.data() + offset, count);
    return true;
  }
  bool write(size_t offset, const void* input, size_t count) override {
    if (!range(offset, count)) return false;
    ++writes;
    const auto* data = static_cast<const uint8_t*>(input);
    const size_t written = write_budget < 0 ? count : std::min<size_t>(write_budget, count);
    for (size_t i = 0; i < written; ++i) {
      assert((bytes[offset + i] & data[i]) == data[i]);  // Real NOR cannot turn 0 back into 1.
      bytes[offset + i] &= data[i];
    }
    if (write_budget >= 0) write_budget -= written;
    return written == count;
  }
  bool erase(size_t offset, size_t count) override {
    assert(offset % Layout::SECTOR_SIZE == 0 && count % Layout::SECTOR_SIZE == 0);
    if (!range(offset, count)) return false;
    ++erases;
    const size_t erased = erase_budget < 0 ? count : std::min<size_t>(erase_budget, count);
    std::fill(bytes.begin() + offset, bytes.begin() + offset + erased, 0xFF);
    if (erase_budget >= 0) erase_budget -= erased;
    return erased == count;
  }
  void assert_archives_untouched() const {
    assert(std::all_of(bytes.begin(), bytes.begin() + Layout::ODU_UPDATE_OFFSET,
                       [](uint8_t value) { return value == 0x3C; }));
    assert(std::all_of(bytes.begin() + Layout::ODU_UPDATE_END_OFFSET, bytes.end(),
                       [](uint8_t value) { return value == 0xFF; }));
  }
};
