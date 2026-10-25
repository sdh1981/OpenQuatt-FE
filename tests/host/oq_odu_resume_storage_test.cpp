#include <cassert>
#include <iostream>
#include <vector>
#include "tests/odu_firmware_bus/flash_model.h"

using namespace oq_odu_firmware;

Session fixture_session() {
  Session value;
  value.session_id = 1;
  value.slave = value.hp = value.topology = 1;
  value.source_version = 0x0200;
  memcpy(value.serial, "SIM-ODU-0000001", 15);
  memcpy(value.image_sha256, IMAGE_SHA256, sizeof(IMAGE_SHA256));
  value.authorized = 1;
  return value;
}

int main() {
  unsigned cases = 0;
  std::vector<uint8_t> image(IMAGE_SIZE, 0x5A);
  FlashMemory baseline;
  Storage original(baseline);
  Session current = fixture_session();
  assert(original.save_session(current));
  for (int cut = 0; cut <= 128; ++cut) {
    FlashMemory flash = baseline;
    Storage storage(flash);
    Session old;
    assert(storage.load_session(old) == Storage::Journal::VALID);
    auto next = old;
    next.authorized = 2;
    next.acknowledged = 1;
    flash.write_budget = cut;
    const bool saved = storage.save_session(next);
    flash.write_budget = -1;
    Storage rebooted(flash);
    Session recovered;
    assert(rebooted.load_session(recovered) == Storage::Journal::VALID);
    assert(recovered.authorized == (saved ? 2 : 1));
    assert(valid_session(recovered));
    flash.assert_archives_untouched();
    ++cases;
  }
  // Two complete sectors before rollover; every partial erase must preserve
  // the latest committed intent in the other sector.
  FlashMemory full;
  Storage journal(full);
  for (unsigned i = 1; i <= 64; ++i) {
    current.authorized = i;
    current.acknowledged = i - 1;
    assert(journal.save_session(current));
  }
  for (int cut : {0, 1, 127, 1024, 4095}) {
    FlashMemory flash = full;
    Storage storage(flash);
    Session old;
    assert(storage.load_session(old) == Storage::Journal::VALID);
    assert(old.authorized == 64);
    current.authorized = 65;
    current.acknowledged = 64;
    flash.erase_budget = cut;
    assert(!storage.save_session(current));
    flash.erase_budget = -1;
    Storage rebooted(flash);
    Session recovered;
    assert(rebooted.load_session(recovered) == Storage::Journal::VALID && recovered.authorized == 64);
    flash.assert_archives_untouched();
    ++cases;
  }
  {
    FlashMemory flash = full;
    Storage storage(flash);
    Session value;
    assert(storage.load_session(value) == Storage::Journal::VALID);
    for (unsigned i = 65; i <= 1000; ++i) {
      current.authorized = (i % 900) + 1;
      current.acknowledged = current.authorized - 1;
      assert(storage.save_session(current));
    }
    Storage rebooted(flash);
    assert(rebooted.load_session(value) == Storage::Journal::VALID && value.generation == 1000);
    flash.assert_archives_untouched();
    ++cases;
  }
  // Every staging step is a simulated reset point. The header becomes valid
  // only after all erase/write/readback steps, never for a partial file.
  FlashMemory staging;
  Storage writer(staging);
  assert(writer.begin_stage());
  unsigned steps = 0;
  while (writer.staging()) {
    const auto result = writer.step(image.data());
    ++steps;
    Storage rebooted(staging);
    assert(rebooted.has_image() == (result == Storage::Stage::DONE));
    if (result == Storage::Stage::DONE) {
      std::vector<uint8_t> restored(IMAGE_SIZE);
      assert(rebooted.read_image(0, restored.data(), restored.size()) && restored == image);
    }
    staging.assert_archives_untouched();
    ++cases;
  }
  assert(steps == 89);
  for (int cut = 0; cut < 64; ++cut) {
    FlashMemory flash;
    Storage storage(flash);
    assert(storage.begin_stage());
    while (storage.stage() != Storage::Stage::COMMIT_HEADER) storage.step(image.data());
    flash.write_budget = cut;
    assert(storage.step(image.data()) == Storage::Stage::FAILED);
    Storage rebooted(flash);
    assert(!rebooted.has_image());
    ++cases;
  }
  {
    FlashMemory flash;
    Storage storage(flash);
    assert(storage.begin_stage());
    while (storage.stage() != Storage::Stage::VERIFY_IMAGE) storage.step(image.data());
    flash.bytes[Storage::Layout::ODU_UPDATE_IMAGE_OFFSET + 100] ^= 1;
    assert(storage.step(image.data()) == Storage::Stage::FAILED && !storage.has_image());
    ++cases;
  }
  {
    FlashMemory small(0x1E0000);
    Storage storage(small);
    Session value;
    assert(!storage.available() && !storage.begin_stage() && !storage.save_session(current));
    assert(storage.load_session(value) == Storage::Journal::UNAVAILABLE);
    assert(small.writes == 0 && small.erases == 0);
    ++cases;
  }
  {
    FlashMemory flash;
    Storage storage(flash);
    Session invalid = current;
    invalid.slave = 0;
    assert(!storage.save_session(invalid));
    invalid = current;
    invalid.image_sha256[0] ^= 1;
    assert(!storage.save_session(invalid));
    invalid = current;
    memset(invalid.serial, 0, sizeof(invalid.serial));
    assert(!storage.save_session(invalid));
    invalid = current;
    invalid.complete = 1;
    assert(!storage.save_session(invalid));
    assert(flash.writes == 0 && flash.erases == 0);
    ++cases;
  }
  {
    FlashMemory flash;
    Session previous = fixture_session();
    previous.generation = 0xFFFFFFFEU;
    previous.crc = checksum(&previous, offsetof(Session, crc));
    assert(flash.write(Storage::Layout::ODU_UPDATE_OFFSET, &previous, sizeof(previous)));
    Storage storage(flash);
    Session saved;
    assert(storage.load_session(saved) == Storage::Journal::VALID);
    assert(storage.save_session(previous));
    assert(storage.save_session(previous));
    Storage rebooted(flash);
    assert(rebooted.load_session(saved) == Storage::Journal::VALID && saved.generation == 0);
    ++cases;
  }
  std::cout << "PASS " << cases << " persistent-cache/journal scenarios, including every torn-record byte\n";
}
