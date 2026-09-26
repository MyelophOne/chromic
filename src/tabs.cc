#include "tabs.h"

#include <windows.h>

#include <cstdlib>

#include "config.h"
#include "uia.h"
#include "utils.h"

namespace {

HHOOK g_mouse_hook = nullptr;
HHOOK g_keyboard_hook = nullptr;
POINT g_left_down{-1, -1};

enum class CloseKind { kMiddle, kCloseButton, kKeyboard, kCount };

bool NeedKeep(int count, CloseKind kind) {
  if (!GetConfig().keep_last_tab() || count <= 0) return false;
  static ULONGLONG last[static_cast<int>(CloseKind::kCount)]{};
  const int index = static_cast<int>(kind);
  const ULONGLONG now = GetTickCount64();
  const ULONGLONG elapsed = now - last[index];
  last[index] = now;
  return count == 1 || (count == 2 && elapsed > 50 && elapsed <= 250);
}

void ReplaceLastTab(HWND hwnd) {
  ExecuteChromeCommand(hwnd, kChromeNewTab);
  ExecuteChromeCommand(hwnd, kChromeCloseOtherTabs);
}

bool IsDrag(POINT point) {
  const int dx = std::abs(point.x - g_left_down.x);
  const int dy = std::abs(point.y - g_left_down.y);
  return dx > GetSystemMetrics(SM_CXDRAG) || dy > GetSystemMetrics(SM_CYDRAG);
}

bool HandleDoubleClick(const MOUSEHOOKSTRUCT& mouse) {
  if (!GetConfig().double_click_close()) return false;
  const auto hit = FindTabHitResult(mouse.pt, true, true);
  if (!hit || hit->on_close_button) return false;
  const HWND hwnd = WindowFromPoint(mouse.pt);
  if (hit->tab_count == 1 && GetConfig().keep_last_tab()) {
    ReplaceLastTab(hwnd);
  } else {
    ExecuteChromeCommand(hwnd, kChromeCloseTab);
  }
  return true;
}

bool HandleNativeClose(const MOUSEHOOKSTRUCT& mouse, CloseKind kind,
                       bool must_be_close_button) {
  if (!GetConfig().keep_last_tab()) return false;
  const auto hit = FindTabHitResult(mouse.pt, true, must_be_close_button);
  if (!hit || (must_be_close_button && !hit->on_close_button)) return false;
  if (!NeedKeep(hit->tab_count, kind)) return false;
  ReplaceLastTab(WindowFromPoint(mouse.pt));
  return true;
}

LRESULT CALLBACK MouseProc(int code, WPARAM message, LPARAM parameter) {
  if (code != HC_ACTION) {
    return CallNextHookEx(g_mouse_hook, code, message, parameter);
  }
  const auto& mouse = *reinterpret_cast<MOUSEHOOKSTRUCT*>(parameter);
  static bool closing_double = false;
  static bool closing_middle = false;
  static bool prior_left_down_on_tab = false;

  switch (message) {
    case WM_LBUTTONDOWN:
    case WM_NCLBUTTONDOWN:
      closing_double = false;
      prior_left_down_on_tab = false;
      if (message == WM_LBUTTONDOWN) {
        g_left_down = mouse.pt;
        if (GetConfig().double_click_close()) {
          const auto hit = FindTabHitResult(mouse.pt, false, true);
          prior_left_down_on_tab = hit && !hit->on_close_button;
        }
      }
      break;
    case WM_LBUTTONUP:
      if (closing_double) return 1;
      if (!IsDrag(mouse.pt) &&
          HandleNativeClose(mouse, CloseKind::kCloseButton, true)) {
        closing_double = true;
        return 1;
      }
      break;
    case WM_NCLBUTTONUP:
      if (closing_double) return 1;
      break;
    case WM_LBUTTONDBLCLK:
      if (closing_double) return 1;
      if (prior_left_down_on_tab && HandleDoubleClick(mouse)) {
        closing_double = true;
        return 1;
      }
      break;
    case WM_MBUTTONDOWN:
    case WM_NCMBUTTONDOWN:
      closing_middle = false;
      break;
    case WM_MBUTTONUP:
      if (closing_middle) return 1;
      if (HandleNativeClose(mouse, CloseKind::kMiddle, false)) {
        closing_middle = true;
        return 1;
      }
      break;
    case WM_MBUTTONDBLCLK:
      if (closing_middle) return 1;
      break;
  }
  return CallNextHookEx(g_mouse_hook, code, message, parameter);
}

LRESULT CALLBACK KeyboardProc(int code, WPARAM key, LPARAM state) {
  if (code == HC_ACTION && !(state & 0x80000000) &&
      GetConfig().keep_last_tab()) {
    const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    if ((key == 'W' && ctrl && !shift) || (key == VK_F4 && ctrl)) {
      HWND hwnd = GetForegroundWindow();
      if (const HWND owner = GetAncestor(hwnd, GA_ROOTOWNER)) hwnd = owner;
      const auto count = FindTabCount(hwnd);
      if (count && NeedKeep(*count, CloseKind::kKeyboard)) {
        ReplaceLastTab(hwnd);
        return 1;
      }
    }
  }
  return CallNextHookEx(g_keyboard_hook, code, key, state);
}

}

void InstallTabHooks() {
  if (!GetConfig().keep_last_tab() && !GetConfig().double_click_close()) return;
  g_mouse_hook = SetWindowsHookExW(WH_MOUSE, MouseProc, g_module,
                                   GetCurrentThreadId());
  if (GetConfig().keep_last_tab()) {
    g_keyboard_hook = SetWindowsHookExW(WH_KEYBOARD, KeyboardProc, g_module,
                                        GetCurrentThreadId());
  }
}
