#pragma once

#ifdef NDEBUG
#error "Diagnostics tests require active assertions"
#endif
#include <cassert>
#include <cstring>
#include <map>
#include <memory>
#include <optional>
#include <string>

// No flash, NVS, firmware-engine or crypto doubles: references to any such
// interface cause this production-wrapper test to fail to compile/link.
namespace diagnostic_test {
inline uint32_t now{0};
inline bool authenticated{true};
}  // namespace diagnostic_test
constexpr int HTTP_GET = 1, HTTP_POST = 2, HTTPD_RESP_USE_STRLEN = -1;
struct httpd_req_t {
  std::string response, status, type;
};
inline int httpd_resp_set_type(httpd_req_t* req, const char* type) {
  req->type = type;
  return 0;
}
inline int httpd_resp_set_hdr(httpd_req_t*, const char*, const char*) { return 0; }
inline int httpd_resp_send_chunk(httpd_req_t* req, const char* data, int length) {
  if (data) req->response.append(data, length < 0 ? strlen(data) : static_cast<size_t>(length));
  return 0;
}
class AsyncWebServerRequest {
 public:
  static constexpr size_t URL_BUF_SIZE = 128;
  httpd_req_t raw;
  int verb{HTTP_GET};
  std::string path{"/openquatt/odu-diagnostics/status"}, hp{"1"};
  std::map<std::string, std::string> headers{
      {"Host", "local.test"}, {"Origin", "http://local.test"}, {"X-OQ-CSRF", "test-token"}};
  int method() const { return verb; }
  void url_to(char* output) const {
    assert(path.size() < URL_BUF_SIZE);
    strcpy(output, path.c_str());
  }
  std::optional<std::string> get_header(const char* name) const {
    const auto it = headers.find(name);
    return it == headers.end() ? std::nullopt : std::optional<std::string>(it->second);
  }
  std::string arg(const char* name) const { return strcmp(name, "hp") == 0 ? hp : ""; }
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
  virtual bool canHandle(AsyncWebServerRequest*) const = 0;
  virtual void handleRequest(AsyncWebServerRequest*) = 0;
};
namespace esphome {
inline uint32_t millis() { return diagnostic_test::now; }
namespace setup_priority {
constexpr float WIFI = 200;
}
class Component {
 public:
  virtual ~Component() = default;
  virtual void setup() {}
  virtual void loop() {}
  virtual float get_setup_priority() const { return 0; }
};
namespace openquatt_web_auth {
class OpenQuattWebAuth {
 public:
  bool request_is_authenticated_admin(AsyncWebServerRequest*) const { return diagnostic_test::authenticated; }
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
