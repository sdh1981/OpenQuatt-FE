#pragma once
#define LOG_STR(value) nullptr
template <typename... Args>
void fake_log(Args&&...) {}
#define ESP_LOGD(...) fake_log(__VA_ARGS__)
