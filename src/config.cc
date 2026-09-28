#include "config.h"

#include <windows.h>

#include "utils.h"

Config::Config()
    : command_line_(ReadIni(L"general", L"command_line")),
      launch_on_startup_(ReadIni(L"general", L"launch_on_startup")),
      launch_on_exit_(ReadIni(L"general", L"launch_on_exit")),
      data_dir_(LoadDirectory(L"data_dir", L"%app%\\..\\User Data")),
      cache_dir_(LoadDirectory(L"cache_dir", L"")),
      ignore_policies_(GetPrivateProfileIntW(L"general", L"ignore_policies", 0,
                                              IniPath().c_str()) != 0),
      keep_last_tab_(GetPrivateProfileIntW(L"tabs", L"keep_last_tab", 1,
                                           IniPath().c_str()) != 0),
      double_click_close_(GetPrivateProfileIntW(
                              L"tabs", L"double_click_close", 1,
                              IniPath().c_str()) != 0) {}

std::optional<std::wstring> Config::LoadDirectory(std::wstring_view key,
                                                  std::wstring_view fallback) {
  std::wstring value = ReadIni(L"general", key, fallback);
  if (value == L"none") return std::nullopt;
  if (value.empty()) value = std::wstring(fallback);
  if (value.empty()) return std::nullopt;
  return ExpandPath(std::move(value));
}

const Config& GetConfig() {
  static const Config config;
  return config;
}
