#pragma once

#include <algorithm>
#include <array>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "tests/odu_firmware_bus/model.h"
#include "tests/odu_firmware_bus/flash_model.h"

namespace harness {
inline FlashMemory* flash{nullptr};
inline Device* device{nullptr};
inline std::vector<uint8_t> reference_image;
inline uint32_t now{0}, absolute_ms{0}, nvs_value{0}, nvs_candidate{0};
inline bool nvs_exists{false}, nvs_staged{false}, nvs_save_fails{false}, nvs_sync_fails{false};
inline bool psram_fails{false}, sha_fails{false}, authenticated{true}, enabled{false}, guard{true};
inline bool auth_configured{true};
inline unsigned sha_calls{0}, hold_calls{0};
}  // namespace harness

constexpr int ESP_OK = 0, ESP_FAIL = -1, ESP_PARTITION_TYPE_DATA = 1, ESP_PARTITION_SUBTYPE_ANY = 255;
struct esp_partition_t {
  size_t size;
};
inline const esp_partition_t* esp_partition_find_first(int, int, const char*) {
  static esp_partition_t partition;
  if (!harness::flash) return nullptr;
  partition.size = harness::flash->size();
  return &partition;
}
inline int esp_partition_read(const esp_partition_t*, size_t offset, void* data, size_t size) {
  return harness::flash->read(offset, data, size) ? ESP_OK : ESP_FAIL;
}
inline int esp_partition_write(const esp_partition_t*, size_t offset, const void* data, size_t size) {
  return harness::flash->write(offset, data, size) ? ESP_OK : ESP_FAIL;
}
inline int esp_partition_erase_range(const esp_partition_t*, size_t offset, size_t size) {
  return harness::flash->erase(offset, size) ? ESP_OK : ESP_FAIL;
}

// Crypto library double: exact fixture equality, independent of production
// CRCs. This tests the wrapper's SHA gate, not mbedTLS's SHA implementation.
inline int mbedtls_sha256(const uint8_t* data, size_t size, uint8_t* digest, int) {
  ++harness::sha_calls;
  if (harness::sha_fails) return -1;
  if (size == harness::reference_image.size() && std::equal(data, data + size, harness::reference_image.begin()))
    memcpy(digest, oq_odu_firmware::IMAGE_SHA256, 32);
  else
    memset(digest, 0, 32);
  return 0;
}

constexpr int HTTP_GET = 1, HTTP_POST = 2;
constexpr int HTTPD_RESP_USE_STRLEN = -1;
struct httpd_req_t {
  std::string response, status, type;
};
inline int httpd_resp_set_status(httpd_req_t* req, const char* value) {
  req->status = value;
  return 0;
}
inline int httpd_resp_set_type(httpd_req_t* req, const char* value) {
  req->type = value;
  return 0;
}
inline int httpd_resp_set_hdr(httpd_req_t*, const char*, const char*) { return 0; }
inline int httpd_resp_send(httpd_req_t* req, const char* value, int length) {
  req->response.assign(value, length < 0 ? strlen(value) : static_cast<size_t>(length));
  return 0;
}
class AsyncWebServerRequest {
 public:
  static constexpr size_t URL_BUF_SIZE = 128;
  httpd_req_t raw;
  int verb{HTTP_GET};
  std::string path{"/openquatt/odu-firmware/status"}, hp{"1"};
  std::map<std::string, std::string> headers{
      {"Host", "offline.test"}, {"Origin", "http://offline.test"}, {"X-OQ-CSRF", "test-token"}};
  int method() const { return verb; }
  void url_to(char* output) const {
    assert(path.size() < URL_BUF_SIZE);
    strcpy(output, path.c_str());
  }
  std::optional<std::string> get_header(const char* key) const {
    const auto it = headers.find(key);
    if (it == headers.end()) return std::nullopt;
    return it->second;
  }
  std::string arg(const char* name) { return strcmp(name, "hp") == 0 ? hp : ""; }
  void requestAuthentication() { raw.status = "401"; }
  void send(int code, const char* type, const char* data) {
    raw.status = std::to_string(code);
    raw.type = type;
    raw.response = data;
  }
  operator httpd_req_t*() { return &raw; }
};
class AsyncWebHandler {
 public:
  virtual ~AsyncWebHandler() = default;
  virtual bool canHandle(AsyncWebServerRequest*) const { return false; }
  virtual void handleBody(AsyncWebServerRequest*, uint8_t*, size_t, size_t, size_t) {}
  virtual void handleRequest(AsyncWebServerRequest*) {}
};

namespace esphome {
namespace setup_priority {
constexpr float WIFI = 200;
}
inline uint32_t millis() { return harness::now; }
class Component {
 public:
  virtual ~Component() = default;
  virtual void setup() {}
  virtual void loop() {}
  virtual float get_setup_priority() const { return 0; }
};
class ESPPreferenceObject {
 public:
  template <typename T>
  bool load(T* value) {
    static_assert(sizeof(T) == 4);
    if (!harness::nvs_exists) return false;
    memcpy(value, &harness::nvs_value, 4);
    return true;
  }
  template <typename T>
  bool save(const T* value) {
    static_assert(sizeof(T) == 4);
    if (harness::nvs_save_fails) return false;
    memcpy(&harness::nvs_candidate, value, 4);
    harness::nvs_staged = true;
    return true;
  }
};
class Preferences {
 public:
  template <typename T>
  ESPPreferenceObject make_preference(uint32_t key, bool) {
    static_assert(sizeof(T) == 4);
    assert(key == 0x4F514655U);
    return {};
  }
  bool sync() {
    if (harness::nvs_sync_fails) return false;
    if (harness::nvs_staged) {
      harness::nvs_value = harness::nvs_candidate;
      harness::nvs_exists = true;
      harness::nvs_staged = false;
    }
    return true;
  }
};
inline Preferences preferences;
inline Preferences* global_preferences = &preferences;
namespace openquatt_common {
template <typename T>
class PsramBuffer {
 public:
  bool allocate_external(size_t count) {
    if (harness::psram_fails) return false;
    values_.resize(count);
    return true;
  }
  T* data() { return values_.data(); }
  const T* data() const { return values_.data(); }
  size_t size() const { return values_.size(); }
  explicit operator bool() const { return !values_.empty(); }

 private:
  std::vector<T> values_;
};
}  // namespace openquatt_common
namespace openquatt_modbus {
// Transport double only; the real shared hub's lease logic has its own test.
class OpenQuattModbusClientHub {
 public:
  const void* owner{nullptr};
  bool begin_maintenance(const void* value) {
    if (owner || !harness::device->acquire()) return false;
    owner = value;
    return true;
  }
  bool maintenance_ready(const void* value) const { return owner == value && harness::device->ready(); }
  bool maintenance_send(const void* value, const uint8_t* data, size_t size) {
    assert(owner == value);
    oq_odu_firmware::Storage storage(*harness::flash);
    auto& device = *harness::device;
    assert(storage.load_session(device.durable) == oq_odu_firmware::Storage::Journal::VALID || data[1] == 3);
    device.have_durable = oq_odu_firmware::valid_session(device.durable);
    device.pending = harness::nvs_value != 0;
    return device.send(data, size);
  }
  bool maintenance_read(const void* value, uint8_t& byte) { return owner == value && harness::device->read(byte); }
  bool end_maintenance(const void* value) {
    if (owner != value || !harness::device->release()) return false;
    owner = nullptr;
    return true;
  }
};
}  // namespace openquatt_modbus
namespace openquatt_web_auth {
class OpenQuattWebAuth {
 public:
  bool request_is_authenticated(AsyncWebServerRequest*) const {
    return !harness::auth_configured || harness::authenticated;
  }
  bool request_is_authenticated_admin(AsyncWebServerRequest*) const {
    return harness::auth_configured && harness::authenticated;
  }
  std::string get_csrf_token() const { return "test-token"; }
};
}  // namespace openquatt_web_auth
namespace web_server_base {
struct Base {
  std::unique_ptr<AsyncWebHandler> handler;
  void add_handler(AsyncWebHandler* value) { handler.reset(value); }
};
inline Base server;
inline Base* global_web_server_base = &server;
}  // namespace web_server_base
}  // namespace esphome
