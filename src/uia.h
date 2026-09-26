#pragma once

#include <windows.h>
#include <uiautomation.h>
#include <wrl/client.h>

#include <optional>

struct TabHitResult {
  Microsoft::WRL::ComPtr<IUIAutomationElement> tab;
  int tab_count = 0;
  bool on_close_button = false;
};

std::optional<TabHitResult> FindTabHitResult(POINT point, bool need_count,
                                             bool need_close_button);
std::optional<int> FindTabCount(HWND window);
