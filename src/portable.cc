#include "portable.h"

#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

#include "config.h"
#include "utils.h"

namespace {

std::vector<std::wstring> Parse(std::wstring_view command_line,
                                bool skip_first) {
  std::vector<std::wstring> result;
  if (command_line.empty()) return result;
  std::wstring owned(command_line);
  int count = 0;
  LPWSTR* values = CommandLineToArgvW(owned.c_str(), &count);
  if (!values) return result;
  for (int index = skip_first ? 1 : 0; index < count; ++index) {
    result.emplace_back(values[index]);
  }
  LocalFree(values);
  return result;
}

bool HasSwitch(const std::vector<std::wstring>& args,
               std::wstring_view prefix) {
  for (const auto& arg : args) {
    if (arg == prefix || arg.starts_with(std::wstring(prefix) + L"=")) {
      return true;
    }
  }
  return false;
}

void MergeDisableFeatures(std::vector<std::wstring>& args) {
  constexpr std::wstring_view prefix = L"--disable-features=";
  std::wstring features;
  for (auto it = args.begin(); it != args.end();) {
    if (it->starts_with(prefix)) {
      if (!features.empty()) features.push_back(L',');
      features += it->substr(prefix.size());
      it = args.erase(it);
    } else {
      ++it;
    }
  }
  if (!features.empty()) features.push_back(L',');

  features += L"WinSboxNoFakeGdiInit";
  args.emplace_back(std::wstring(prefix) + features);
}

}

bool RelaunchPortable() {
  std::vector<std::wstring> args = Parse(GetCommandLineW(), true);
  auto sentinel = std::find(args.begin(), args.end(), L"--");
  std::vector<std::wstring> trailing;
  if (sentinel != args.end()) {
    trailing.assign(sentinel, args.end());
    args.erase(sentinel, args.end());
  }
  if (HasSwitch(args, L"--portable")) return false;

  const auto configured = Parse(L"chromic.exe " + GetConfig().command_line(),
                                true);
  args.insert(args.end(), configured.begin(), configured.end());
  args.emplace_back(L"--portable");
  MergeDisableFeatures(args);

  if (!HasSwitch(args, L"--user-data-dir")) {
    if (const auto& value = GetConfig().data_dir()) {
      args.emplace_back(L"--user-data-dir=" + *value);
    }
  }
  if (!HasSwitch(args, L"--disk-cache-dir")) {
    if (const auto& value = GetConfig().cache_dir()) {
      args.emplace_back(L"--disk-cache-dir=" + *value);
    }
  }
  args.insert(args.end(), trailing.begin(), trailing.end());

  std::vector<wchar_t> executable(32768);
  if (!GetModuleFileNameW(nullptr, executable.data(), executable.size())) {
    MessageBoxW(nullptr, L"Chromic could not resolve the browser executable.",
                L"Chromic", MB_OK | MB_ICONERROR);
    ExitProcess(ERROR_PATH_NOT_FOUND);
  }
  std::wstring command = QuoteArg(executable.data()) + L" " + JoinArgs(args);
  std::vector<wchar_t> mutable_command(command.begin(), command.end());
  mutable_command.push_back(L'\0');
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  startup.dwFlags = STARTF_USESHOWWINDOW;
  startup.wShowWindow = SW_SHOWNORMAL;
  PROCESS_INFORMATION process{};
  if (!CreateProcessW(executable.data(), mutable_command.data(), nullptr,
                      nullptr, FALSE, 0, nullptr, AppDir().c_str(), &startup,
                      &process)) {
    const DWORD error = GetLastError();
    MessageBoxW(nullptr,
                L"Chromic could not restart Chrome with the configured "
                L"user-data directory. Startup was cancelled.",
                L"Chromic", MB_OK | MB_ICONERROR);
    ExitProcess(error ? error : ERROR_PROCESS_ABORTED);
  }
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  ExitProcess(0);
}
