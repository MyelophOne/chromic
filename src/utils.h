#pragma once

#include <windows.h>

#include <string>
#include <string_view>
#include <vector>

extern HMODULE g_module;

const std::wstring& AppDir();
const std::wstring& IniPath();
std::wstring ReadIni(std::wstring_view section, std::wstring_view key,
                     std::wstring_view fallback = L"");
std::wstring ExpandPath(std::wstring path);
std::wstring QuoteArg(std::wstring_view arg);
std::wstring JoinArgs(const std::vector<std::wstring>& args);
void RunConfiguredCommands(const std::wstring& commands);
void ExecuteChromeCommand(HWND hwnd, int command_id);
bool IsChromeWindow(HWND hwnd);

template <typename... Args>
void DebugLog(std::wstring_view, Args&&...) {}

inline constexpr int kChromeNewTab = 34014;
inline constexpr int kChromeCloseTab = 34015;
inline constexpr int kChromeCloseOtherTabs = 35023;
