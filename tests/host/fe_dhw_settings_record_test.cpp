// OpenQuatt FE - host-test voor het DHW-instellingenrecord.
#include <assert.h>
#include <math.h>

#include "../../openquatt/includes/fe/oq_fe_dhw_settings_record.h"

using namespace oq_fe_dhw_settings;

namespace {

Record sample() {
  Record record = empty_record();
  for (size_t i = 0; i < kNumberCount; ++i) record.numbers[i] = 40.0f + static_cast<float>(i);
  set_switch_bit(record, 0, true);
  set_switch_bit(record, 9, true);
  record.select = 1;
  return record;
}

void test_seal_makes_valid() {
  Record record = sample();
  assert(!is_valid(record));
  seal(record);
  assert(is_valid(record));
}

void test_corruption_is_rejected() {
  Record record = sample();
  seal(record);
  record.numbers[3] += 1.0f;
  assert(!is_valid(record));

  Record other = sample();
  seal(other);
  other.version = kVersion + 1;
  assert(!is_valid(other));

  Record zero = empty_record();
  assert(!is_valid(zero));
}

void test_switch_bits() {
  Record record = empty_record();
  set_switch_bit(record, 4, true);
  assert(switch_bit(record, 4));
  assert(!switch_bit(record, 3));
  set_switch_bit(record, 4, false);
  assert(!switch_bit(record, 4));
  assert(record.switches == 0);
}

void test_same_values_ignores_header() {
  Record a = sample();
  Record b = sample();
  seal(b);
  assert(same_values(a, b));
  b.select = 0;
  assert(!same_values(a, b));
  Record c = sample();
  c.numbers[0] = NAN;
  Record d = sample();
  d.numbers[0] = NAN;
  assert(same_values(c, d));
}

void test_settle_timer() {
  SettleTimer timer;
  assert(!timer.update(false, 1000, 5000));
  assert(!timer.update(true, 1000, 5000));  // wijziging gezien
  assert(!timer.update(true, 3000, 5000));
  assert(timer.update(true, 6000, 5000));  // 5 s rust: opslaan
  timer.restart(6000);                     // mislukt: opnieuw wachten
  assert(!timer.update(true, 8000, 5000));
  assert(timer.update(true, 11000, 5000));
  assert(!timer.update(false, 12000, 5000));  // opgeslagen: niets meer te doen
  // millis()-overloop
  assert(!timer.update(true, 0xFFFFF000u, 5000));
  assert(timer.update(true, 0x00000800u + 5000u, 5000));
}

}  // namespace

int main() {
  test_seal_makes_valid();
  test_corruption_is_rejected();
  test_switch_bits();
  test_same_values_ignores_header();
  test_settle_timer();
  return 0;
}
