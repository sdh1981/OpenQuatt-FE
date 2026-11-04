// OpenQuatt FE - één NVS-record voor alle DHW-instellingen (fe/nvs-voorstel.md,
// stap 2). Losse ESPHome-preferences kosten elk 3 NVS-entries; dit record van
// 156 bytes kost er 7.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace oq_fe_dhw_settings {

constexpr uint32_t kMagic = 0x53444546u;  // "FEDS"
// Verhoog de versie bij elke wijziging in de volgorde of het aantal velden:
// een record met een andere versie wordt niet geladen.
constexpr uint16_t kVersion = 1;
constexpr size_t kNumberCount = 35;
constexpr size_t kSwitchCount = 10;
constexpr uint8_t kNoSelect = 0xFF;

struct Record {
  uint32_t magic;
  uint16_t version;
  uint8_t number_count;
  uint8_t switch_count;
  float numbers[kNumberCount];
  uint16_t switches;  // bit i = schakelaar i
  uint8_t select;     // index van de gekozen optie, of kNoSelect
  // Maximale watertemperatuur tijdens warm water in hele °C (fe.16). Was een
  // reservebyte die altijd 0 was: 0 betekent "niet gezet", dus de standaard.
  // Zo laden records van vóór fe.16 zonder versiewissel.
  uint8_t dhw_max_water_c;
  uint32_t crc;
};
static_assert(sizeof(Record) == 156, "DHW settings record layout changed; bump kVersion");
static_assert(kSwitchCount <= 16, "switches must fit in uint16_t");

inline uint32_t fnv1a(const uint8_t* data, size_t len) {
  uint32_t hash = 2166136261u;
  for (size_t i = 0; i < len; ++i) {
    hash ^= data[i];
    hash *= 16777619u;
  }
  return hash;
}

inline Record empty_record() {
  Record record;
  std::memset(&record, 0, sizeof(record));
  record.select = kNoSelect;
  return record;
}

inline uint32_t record_crc(const Record& record) {
  return fnv1a(reinterpret_cast<const uint8_t*>(&record), offsetof(Record, crc));
}

inline void seal(Record& record) {
  record.magic = kMagic;
  record.version = kVersion;
  record.number_count = static_cast<uint8_t>(kNumberCount);
  record.switch_count = static_cast<uint8_t>(kSwitchCount);
  record.crc = record_crc(record);
}

inline bool is_valid(const Record& record) {
  return record.magic == kMagic && record.version == kVersion && record.number_count == kNumberCount &&
         record.switch_count == kSwitchCount && record.crc == record_crc(record);
}

// Bitgewijs vergelijken: zo telt ook NAN -> NAN als "gelijk".
inline bool same_values(const Record& a, const Record& b) {
  return std::memcmp(a.numbers, b.numbers, sizeof(a.numbers)) == 0 && a.switches == b.switches &&
         a.select == b.select && a.dhw_max_water_c == b.dhw_max_water_c;
}

// DHW-watergrens als byte: afgerond op hele graden; buiten 1..255 of ongeldig
// wordt het 0 (niet gezet).
inline uint8_t encode_whole_c(float value_c) {
  if (!(value_c >= 1.0f && value_c <= 255.0f)) return 0;
  return static_cast<uint8_t>(value_c + 0.5f);
}

// 0 = niet gezet: dan blijft de standaardwaarde van de entiteit staan.
inline bool decode_whole_c(uint8_t stored, float& value_c) {
  if (stored == 0) return false;
  value_c = static_cast<float>(stored);
  return true;
}

inline bool switch_bit(const Record& record, size_t index) { return ((record.switches >> index) & 1u) != 0; }

inline void set_switch_bit(Record& record, size_t index, bool on) {
  if (on) {
    record.switches = static_cast<uint16_t>(record.switches | (1u << index));
  } else {
    record.switches = static_cast<uint16_t>(record.switches & ~(1u << index));
  }
}

// Opslaan pas na een rustperiode, zodat een back-up terugzetten of een reeks
// wijzigingen één write wordt. Geeft true als er nu opgeslagen moet worden.
class SettleTimer {
 public:
  bool update(bool changed, uint32_t now_ms, uint32_t settle_ms) {
    if (!changed) {
      since_ms_ = 0;
      armed_ = false;
      return false;
    }
    if (!armed_) {
      armed_ = true;
      since_ms_ = now_ms;
      return false;
    }
    return static_cast<uint32_t>(now_ms - since_ms_) >= settle_ms;
  }

  // Na een geslaagde of mislukte save: opnieuw de rustperiode afwachten.
  void restart(uint32_t now_ms) {
    armed_ = true;
    since_ms_ = now_ms;
  }

 private:
  bool armed_ = false;
  uint32_t since_ms_ = 0;
};

}  // namespace oq_fe_dhw_settings
