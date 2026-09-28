#include "utils.h"

#include <shellapi.h>
#include <shlwapi.h>

#include <array>
#include <utility>

HMODULE g_module = nullptr;

namespace {

std::wstring ExpandVariables(std::wstring path) {
  for (size_t pos = 0; (pos = path.find(L"%app%", pos)) != std::wstring::npos;) {
    path.replace(pos, 5, AppDir());
    pos += AppDir().size();
  }
  DWORD needed = ExpandEnvironmentStringsW(path.c_str(), nullptr, 0);
  if (needed) {
    std::vector<wchar_t> expanded(needed);
    if (ExpandEnvironmentStringsW(path.c_str(), expanded.data(), needed)) {
      path.assign(expanded.data());
    }
  }
  return path;
}

}

const std::wstring& AppDir() {
  static const std::wstring value = [] {
    std::array<wchar_t, 32768> path{};
    const DWORD size = GetModuleFileNameW(g_module, path.data(), path.size());
    if (!size || size == path.size()) return std::wstring{};
    PathRemoveFileSpecW(path.data());
    return std::wstring(path.data());
  }();
  return value;
}

const std::wstring& IniPath() {
  static const std::wstring value = AppDir() + L"\\chromic.ini";
  return value;
}

std::wstring ReadIni(std::wstring_view section, std::wstring_view key,
                     std::wstring_view fallback) {
  std::vector<wchar_t> buffer(256);
  while (true) {
    const DWORD read = GetPrivateProfileStringW(
        section.data(), key.data(), fallback.data(), buffer.data(),
        static_cast<DWORD>(buffer.size()), IniPath().c_str());
    if (read < buffer.size() - 1) return std::wstring(buffer.data(), read);
    buffer.resize(buffer.size() * 2);
  }
}

std::wstring ExpandPath(std::wstring path) {
  path = ExpandVariables(std::move(path));
  if (PathIsRelativeW(path.c_str())) path = AppDir() + L"\\" + path;
  std::vector<wchar_t> absolute(32768);
  const DWORD length = GetFullPathNameW(path.c_str(), absolute.size(),
                                        absolute.data(), nullptr);
  return length && length < absolute.size() ? std::wstring(absolute.data())
                                             : path;
}

std::wstring QuoteArg(std::wstring_view arg) {
  if (arg.find_first_of(L" \t\"") == std::wstring_view::npos) {
    return std::wstring(arg);
  }
  std::wstring out = L"\"";
  size_t slashes = 0;
  for (wchar_t ch : arg) {
    if (ch == L'\\') {
      ++slashes;
    } else if (ch == L'"') {
      out.append(slashes * 2 + 1, L'\\');
      out.push_back(L'"');
      slashes = 0;
    } else {
      out.append(slashes, L'\\');
      slashes = 0;
      out.push_back(ch);
    }
  }
  out.append(slashes * 2, L'\\');
  out.push_back(L'"');
  return out;
}

std::wstring JoinArgs(const std::vector<std::wstring>& args) {
  std::wstring out;
  for (const auto& arg : args) {
    if (!out.empty()) out.push_back(L' ');
    out += QuoteArg(arg);
  }
  return out;
}

void RunConfiguredCommands(const std::wstring& commands) {
  size_t start = 0;
  while (start <= commands.size()) {
    const size_t end = commands.find(L';', start);
    std::wstring command = commands.substr(start, end - start);
    if (!command.empty()) {
      command = ExpandVariables(std::move(command));
      std::wstring line = L"cmd.exe /d /s /c \"" + command + L"\"";
      std::vector<wchar_t> mutable_line(line.begin(), line.end());
      mutable_line.push_back(L'\0');
      STARTUPINFOW startup{};
      startup.cb = sizeof(startup);
      PROCESS_INFORMATION process{};
      if (CreateProcessW(nullptr, mutable_line.data(), nullptr, nullptr, FALSE,
                         CREATE_NO_WINDOW, nullptr, AppDir().c_str(), &startup,
                         &process)) {
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
      }
    }
    if (end == std::wstring::npos) break;
    start = end + 1;
  }
}

void ExecuteChromeCommand(HWND hwnd, int command_id) {
  if (!hwnd) hwnd = GetForegroundWindow();
  if (const HWND root = hwnd ? GetAncestor(hwnd, GA_ROOT) : nullptr) hwnd = root;
  if (hwnd) PostMessageW(hwnd, WM_SYSCOMMAND, command_id, 0);
}

bool IsChromeWindow(HWND hwnd) {
  std::array<wchar_t, 64> name{};
  const int length = GetClassNameW(hwnd, name.data(), name.size());
  return length > 0 && std::wstring_view(name.data(), length).starts_with(
                           L"Chrome_WidgetWin_");
}
