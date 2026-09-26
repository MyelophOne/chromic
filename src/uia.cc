#include "uia.h"

#include <uiautomation.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <initializer_list>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "com_initializer.h"
#include "utils.h"

namespace {

using Microsoft::WRL::ComPtr;

class ScopedVariant {
 public:
  ScopedVariant() { VariantInit(&value_); }
  ~ScopedVariant() { VariantClear(&value_); }
  ScopedVariant(const ScopedVariant&) = delete;
  ScopedVariant& operator=(const ScopedVariant&) = delete;
  ScopedVariant(ScopedVariant&&) = delete;
  ScopedVariant& operator=(ScopedVariant&&) = delete;

  VARIANT* Ptr() { return &value_; }
  VARIANT& Ref() { return value_; }

 private:
  VARIANT value_;
};

struct CachedClassConditions {
  ComPtr<IUIAutomationCondition> tab_strip_drag_context;
  ComPtr<IUIAutomationCondition> tab_container_impl;
  ComPtr<IUIAutomationCondition> vertical_unpinned_tab_container_view;
  ComPtr<IUIAutomationCondition> unpinned_tab_container_view;
  ComPtr<IUIAutomationCondition> tab;
  ComPtr<IUIAutomationCondition> vertical_tab_view;
  ComPtr<IUIAutomationCondition> tab_view;
  ComPtr<IUIAutomationCondition> tab_close_button;
};

enum class TabContainerKind {
  kHorizontal,
  kVertical,

  kUnified,
};

struct TabContainer {
  ComPtr<IUIAutomationElement> element;
  TabContainerKind kind = TabContainerKind::kHorizontal;
};

struct TabUiCache {
  HWND window = nullptr;
  bool fullscreen = false;

  ULONGLONG retry_after_ticks = 0;

  ComPtr<IUIAutomationElement> region;
  TabContainer container;
};

struct UiaSession {
  ComInitializer com_initializer;
  bool init_attempted = false;
  bool init_succeeded = false;
  ComPtr<IUIAutomation> automation;
  ComPtr<IUIAutomationTreeWalker> control_view_walker;
  ComPtr<IUIAutomationTreeWalker> raw_view_walker;
  CachedClassConditions class_conditions;
  TabUiCache tab_ui_cache;
};

UiaSession& GetThreadLocalUiaSession() {

  thread_local UiaSession* session = new UiaSession;
  return *session;
}

bool CreateClassCondition(const ComPtr<IUIAutomation>& automation,
                          std::wstring_view class_name,
                          ComPtr<IUIAutomationCondition>* condition) {
  if (!automation || class_name.empty() || !condition) {
    return false;
  }

  ScopedVariant value;
  value.Ref().vt = VT_BSTR;
  value.Ref().bstrVal = SysAllocStringLen(class_name.data(),
                                          static_cast<UINT>(class_name.size()));
  if (!value.Ref().bstrVal) {
    return false;
  }

  return SUCCEEDED(
      automation->CreatePropertyCondition(UIA_ClassNamePropertyId, value.Ref(),
                                          condition->ReleaseAndGetAddressOf()));
}

bool InitializeClassConditions(UiaSession* session) {
  if (!session || !session->automation) {
    return false;
  }

  auto& conditions = session->class_conditions;
  return CreateClassCondition(session->automation,
                              L"TabStrip::TabDragContextImpl",
                              &conditions.tab_strip_drag_context) &&
         CreateClassCondition(session->automation, L"TabContainerImpl",
                              &conditions.tab_container_impl) &&
         CreateClassCondition(
             session->automation, L"VerticalUnpinnedTabContainerView",
             &conditions.vertical_unpinned_tab_container_view) &&
         CreateClassCondition(session->automation, L"UnpinnedTabContainerView",
                              &conditions.unpinned_tab_container_view) &&
         CreateClassCondition(session->automation, L"Tab", &conditions.tab) &&
         CreateClassCondition(session->automation, L"VerticalTabView",
                              &conditions.vertical_tab_view) &&
         CreateClassCondition(session->automation, L"TabView",
                              &conditions.tab_view) &&
         CreateClassCondition(session->automation, L"TabCloseButton",
                              &conditions.tab_close_button);
}

UiaSession* GetUiaSession() {
  auto& session = GetThreadLocalUiaSession();
  if (session.init_attempted) {
    return session.init_succeeded ? &session : nullptr;
  }
  session.init_attempted = true;

  if (!session.com_initializer.IsInitialized()) {
    DebugLog(L"UIA: COM initialization failed");
    return nullptr;
  }

  if (FAILED(CoCreateInstance(
          CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
          IID_PPV_ARGS(session.automation.ReleaseAndGetAddressOf())))) {
    DebugLog(L"UIA: CoCreateInstance(CUIAutomation) failed");
    return nullptr;
  }

  if (FAILED(session.automation->get_ControlViewWalker(
          &session.control_view_walker)) ||
      !session.control_view_walker) {
    DebugLog(L"UIA: get_ControlViewWalker failed");
    return nullptr;
  }

  if (FAILED(session.automation->get_RawViewWalker(&session.raw_view_walker)) ||
      !session.raw_view_walker) {
    DebugLog(L"UIA: get_RawViewWalker failed");
    return nullptr;
  }

  if (!InitializeClassConditions(&session)) {
    DebugLog(L"UIA: failed to cache class conditions");
    return nullptr;
  }

  session.init_succeeded = true;
  return &session;
}

class ScopedBstr {
 public:
  ScopedBstr() = default;
  ~ScopedBstr() { SysFreeString(bstr_); }
  ScopedBstr(const ScopedBstr&) = delete;
  ScopedBstr& operator=(const ScopedBstr&) = delete;

  BSTR* Receive() { return &bstr_; }
  BSTR Get() const { return bstr_; }
  UINT Length() const { return SysStringLen(bstr_); }
  explicit operator bool() const { return bstr_ != nullptr; }

 private:
  BSTR bstr_ = nullptr;
};

bool BstrEqualsStringView(BSTR bstr, std::wstring_view expected) {
  return bstr != nullptr &&
         std::wstring_view(bstr, SysStringLen(bstr)) == expected;
}

ComPtr<IUIAutomationElement> GetElementFromWindow(const UiaSession& session,
                                                  HWND hwnd) {
  if (!hwnd) {
    return nullptr;
  }

  ComPtr<IUIAutomationElement> element;
  if (FAILED(session.automation->ElementFromHandle(
          hwnd, element.ReleaseAndGetAddressOf())) ||
      !element) {
    return nullptr;
  }
  return element;
}

bool HasClassName(const ComPtr<IUIAutomationElement>& element,
                  std::wstring_view expected_class_name) {
  if (!element) {
    return false;
  }

  ScopedBstr class_name;
  if (FAILED(element->get_CurrentClassName(class_name.Receive())) ||
      !class_name) {
    return false;
  }

  return BstrEqualsStringView(class_name.Get(), expected_class_name);
}

ComPtr<IUIAutomationElement> FindBrowserViewFromTopChrome(
    const UiaSession& session,
    HWND window) {
  RECT client_rect;
  if (!window || !GetClientRect(window, &client_rect) ||
      client_rect.right - client_rect.left <= 1 ||
      client_rect.bottom - client_rect.top <= 1) {
    return nullptr;
  }

  POINT point{client_rect.left + 1, client_rect.top + 1};
  if (!ClientToScreen(window, &point)) {
    return nullptr;
  }

  const HWND point_window = WindowFromPoint(point);
  if (point_window != window || !IsChromeWindow(point_window)) {
    return nullptr;
  }

  ComPtr<IUIAutomationElement> element;
  if (FAILED(session.automation->ElementFromPoint(
          point, element.ReleaseAndGetAddressOf())) ||
      !element) {
    return nullptr;
  }

  constexpr int kMaxAncestorDepth = 12;
  for (int depth = 0; element && depth < kMaxAncestorDepth; ++depth) {
    if (HasClassName(element, L"BrowserView")) {
      return element;
    }

    ComPtr<IUIAutomationElement> parent;
    if (FAILED(session.control_view_walker->GetParentElement(
            element.Get(), parent.ReleaseAndGetAddressOf()))) {
      return nullptr;
    }
    element = std::move(parent);
  }
  return nullptr;
}

template <typename Visitor>
bool TraverseDescendantsRaw(const UiaSession& session,
                            const ComPtr<IUIAutomationElement>& root,
                            Visitor visitor) {
  if (!root || !session.raw_view_walker) {
    return false;
  }

  std::vector<ComPtr<IUIAutomationElement>> stack;
  auto push_children = [&](const ComPtr<IUIAutomationElement>& parent) {
    ComPtr<IUIAutomationElement> child;
    if (FAILED(session.raw_view_walker->GetFirstChildElement(
            parent.Get(), child.ReleaseAndGetAddressOf()))) {
      return;
    }
    while (child) {
      stack.emplace_back(child);
      ComPtr<IUIAutomationElement> next;
      if (FAILED(session.raw_view_walker->GetNextSiblingElement(
              child.Get(), next.ReleaseAndGetAddressOf()))) {
        break;
      }
      child = std::move(next);
    }
  };

  push_children(root);

  while (!stack.empty()) {
    auto node = std::move(stack.back());
    stack.pop_back();
    if (visitor(node)) {
      return true;
    }

    push_children(node);
  }

  return false;
}

ComPtr<IUIAutomationElement> FindFirstDescendantByClass(
    const ComPtr<IUIAutomationElement>& root,
    const ComPtr<IUIAutomationCondition>& class_condition) {
  if (!root || !class_condition) {
    return nullptr;
  }

  ComPtr<IUIAutomationElement> hit;
  if (FAILED(root->FindFirst(TreeScope_Subtree, class_condition.Get(),
                             hit.ReleaseAndGetAddressOf()))) {
    return nullptr;
  }
  return hit;
}

std::optional<int> CountDescendantsByClassRaw(
    const UiaSession& session,
    const ComPtr<IUIAutomationElement>& root,
    std::wstring_view class_name) {
  if (!root || !session.raw_view_walker) {
    return std::nullopt;
  }

  int count = 0;
  TraverseDescendantsRaw(session, root, [&](const auto& node) {
    if (HasClassName(node, class_name)) {
      ++count;
    }
    return false;
  });
  return count;
}

ComPtr<IUIAutomationElement> FindSiblingByClass(
    const UiaSession& session,
    const ComPtr<IUIAutomationElement>& element,
    std::wstring_view class_name) {
  if (!element || !session.control_view_walker) {
    return nullptr;
  }

  for (const bool forward : {true, false}) {
    ComPtr<IUIAutomationElement> current;
    HRESULT hr = forward
                     ? session.control_view_walker->GetNextSiblingElement(
                           element.Get(), current.ReleaseAndGetAddressOf())
                     : session.control_view_walker->GetPreviousSiblingElement(
                           element.Get(), current.ReleaseAndGetAddressOf());
    while (SUCCEEDED(hr) && current) {
      if (HasClassName(current, class_name)) {
        return current;
      }

      ComPtr<IUIAutomationElement> next;
      hr = forward ? session.control_view_walker->GetNextSiblingElement(
                         current.Get(), next.ReleaseAndGetAddressOf())
                   : session.control_view_walker->GetPreviousSiblingElement(
                         current.Get(), next.ReleaseAndGetAddressOf());
      current = std::move(next);
    }
  }

  return nullptr;
}

bool HasNativeWindowHandle(const ComPtr<IUIAutomationElement>& element) {
  if (!element) {
    return false;
  }

  UIA_HWND native_window = nullptr;
  if (FAILED(element->get_CurrentNativeWindowHandle(&native_window))) {
    return false;
  }
  return native_window != nullptr;
}

ComPtr<IUIAutomationElement> FindShallowDescendantByClasses(
    IUIAutomationTreeWalker* walker,
    const ComPtr<IUIAutomationElement>& anchor,
    std::initializer_list<std::wstring_view> target_class_names,
    int max_visited) {
  if (!walker || !anchor) {
    return nullptr;
  }

  constexpr int kMaxDepth = 12;

  struct QueuedElement {
    ComPtr<IUIAutomationElement> element;
    int depth;
  };
  std::vector<QueuedElement> queue;
  size_t next_index = 0;

  auto enqueue_children = [&](const ComPtr<IUIAutomationElement>& parent,
                              int depth) {
    ComPtr<IUIAutomationElement> child;
    if (FAILED(walker->GetFirstChildElement(parent.Get(),
                                            child.ReleaseAndGetAddressOf()))) {
      return;
    }
    while (child) {
      queue.push_back({child, depth});
      ComPtr<IUIAutomationElement> sibling;
      if (FAILED(walker->GetNextSiblingElement(
              child.Get(), sibling.ReleaseAndGetAddressOf()))) {
        break;
      }
      child = std::move(sibling);
    }
  };

  enqueue_children(anchor, 1);

  int visited = 0;
  while (next_index < queue.size()) {

    const QueuedElement current = std::move(queue[next_index]);
    ++next_index;
    if (++visited > max_visited) {
      DebugLog(L"UIA: chrome-only BFS exhausted its element budget");
      return nullptr;
    }

    ScopedBstr class_name;
    if (FAILED(current.element->get_CurrentClassName(class_name.Receive()))) {
      continue;
    }
    const std::wstring_view class_name_view =
        class_name ? std::wstring_view(class_name.Get(), class_name.Length())
                   : std::wstring_view();
    if (std::ranges::contains(target_class_names, class_name_view)) {
      return current.element;
    }

    if (current.depth >= kMaxDepth) {
      continue;
    }
    if (std::ranges::contains(
            std::initializer_list<std::wstring_view>{
                L"MultiContentsView", L"WebView", L"ContentsWebView"},
            class_name_view) ||
        HasNativeWindowHandle(current.element)) {
      continue;
    }
    enqueue_children(current.element, current.depth + 1);
  }

  return nullptr;
}

TreeScope GetTabElementScope(TabContainerKind kind) {

  return kind == TabContainerKind::kHorizontal ? TreeScope_Children
                                               : TreeScope_Subtree;
}

std::wstring_view GetTabElementClassName(TabContainerKind kind) {
  switch (kind) {
    case TabContainerKind::kHorizontal:
      return L"Tab";
    case TabContainerKind::kVertical:
      return L"VerticalTabView";
    case TabContainerKind::kUnified:
      return L"TabView";
  }
  return L"Tab";
}

const ComPtr<IUIAutomationCondition>& GetTabElementCondition(
    const UiaSession& session,
    TabContainerKind kind) {
  switch (kind) {
    case TabContainerKind::kHorizontal:
      return session.class_conditions.tab;
    case TabContainerKind::kVertical:
      return session.class_conditions.vertical_tab_view;
    case TabContainerKind::kUnified:
      return session.class_conditions.tab_view;
  }
  return session.class_conditions.tab;
}

bool IsWindowFullScreen(HWND hwnd) {
  RECT window_rect;
  if (!GetWindowRect(hwnd, &window_rect)) {
    return false;
  }

  const HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
  if (!monitor) {
    return false;
  }

  MONITORINFO monitor_info{};
  monitor_info.cbSize = sizeof(monitor_info);
  if (!GetMonitorInfoW(monitor, &monitor_info)) {
    return false;
  }

  return window_rect.left == monitor_info.rcMonitor.left &&
         window_rect.top == monitor_info.rcMonitor.top &&
         window_rect.right == monitor_info.rcMonitor.right &&
         window_rect.bottom == monitor_info.rcMonitor.bottom;
}

std::optional<TabContainer> FindTabContainerInRegion(
    const UiaSession& session,
    const ComPtr<IUIAutomationElement>& region,
    bool vertical) {
  if (vertical) {
    if (const auto container = FindFirstDescendantByClass(
            region,
            session.class_conditions.vertical_unpinned_tab_container_view)) {
      return TabContainer{container, TabContainerKind::kVertical};
    }

    if (const auto container = FindFirstDescendantByClass(
            region, session.class_conditions.unpinned_tab_container_view)) {
      return TabContainer{container, TabContainerKind::kUnified};
    }
    return std::nullopt;
  }

  if (const auto tab_strip = FindFirstDescendantByClass(
          region, session.class_conditions.tab_strip_drag_context)) {
    if (const auto container =
            FindSiblingByClass(session, tab_strip, L"TabContainerImpl")) {
      return TabContainer{container, TabContainerKind::kHorizontal};
    }
  }

  if (const auto container = FindFirstDescendantByClass(
          region, session.class_conditions.tab_container_impl)) {
    return TabContainer{container, TabContainerKind::kHorizontal};
  }
  return std::nullopt;
}

TabUiCache* ResolveTabUi(UiaSession* session, HWND hwnd) {
  TabUiCache& cache = session->tab_ui_cache;
  const bool fullscreen = IsWindowFullScreen(hwnd);
  if (cache.window == hwnd && cache.fullscreen == fullscreen) {
    if (cache.container.element) {
      return &cache;
    }
    if (GetTickCount64() < cache.retry_after_ticks) {
      return nullptr;
    }
  }

  cache = TabUiCache();
  cache.window = hwnd;
  cache.fullscreen = fullscreen;
  cache.retry_after_ticks = GetTickCount64() + 1000;

  const auto window_element = GetElementFromWindow(*session, hwnd);
  if (!window_element) {
    return nullptr;
  }

  auto region = FindShallowDescendantByClasses(
      session->control_view_walker.Get(), window_element,
      {L"HorizontalTabStripRegionView", L"HorizontalTabStripRegionViewOld",
       L"VerticalTabStripRegionView"},
       256);
  if (!region && !fullscreen &&
      FindShallowDescendantByClasses(session->control_view_walker.Get(),
                                     window_element, {L"FindBarView"},
                                      32)) {

    DebugLog(L"UIA: recovering tab UI while find bar is visible");
    const auto browser_view = FindBrowserViewFromTopChrome(*session, hwnd);
    if (!browser_view) {
      DebugLog(L"UIA: failed to recover BrowserView from browser chrome");
      return nullptr;
    }
    region = FindShallowDescendantByClasses(
        session->control_view_walker.Get(), browser_view,
        {L"HorizontalTabStripRegionView", L"HorizontalTabStripRegionViewOld",
         L"VerticalTabStripRegionView"},
         256);
    if (!region) {
      DebugLog(L"UIA: recovered BrowserView has no tab strip region");
    }
  }

  if (region) {
    const bool vertical = HasClassName(region, L"VerticalTabStripRegionView");
    if (auto container = FindTabContainerInRegion(*session, region, vertical)) {
      cache.region = region;
      cache.container = std::move(*container);
      return &cache;
    }
    return nullptr;
  }

  if (fullscreen) {

    if (const auto container = FindShallowDescendantByClasses(
            session->raw_view_walker.Get(), window_element,
            {L"TabContainerImpl", L"VerticalUnpinnedTabContainerView",
             L"UnpinnedTabContainerView"},
             512)) {
      TabContainerKind kind = TabContainerKind::kHorizontal;
      if (HasClassName(container, L"VerticalUnpinnedTabContainerView")) {
        kind = TabContainerKind::kVertical;
      } else if (HasClassName(container, L"UnpinnedTabContainerView")) {
        kind = TabContainerKind::kUnified;
      }
      cache.container = TabContainer{container, kind};
      return &cache;
    }
  }

  return nullptr;
}

TabUiCache* GetValidatedTabUi(UiaSession* session, HWND hwnd, RECT* gate_rect) {
  for (int attempt = 0; attempt < 2; ++attempt) {
    TabUiCache* ui = ResolveTabUi(session, hwnd);
    if (!ui) {
      return nullptr;
    }

    const ComPtr<IUIAutomationElement>& gate =
        ui->region ? ui->region : ui->container.element;
    if (SUCCEEDED(gate->get_CurrentBoundingRectangle(gate_rect))) {
      if (!ui->region) {

        return ui;
      }
      RECT container_rect;
      if (!IsRectEmpty(gate_rect) &&
          SUCCEEDED(ui->container.element->get_CurrentBoundingRectangle(
              &container_rect)) &&
          !IsRectEmpty(&container_rect)) {
        return ui;
      }
    }
    session->tab_ui_cache = TabUiCache();
  }
  return nullptr;
}

ComPtr<IUIAutomationElementArray> FindTabElements(
    const UiaSession& session,
    const TabContainer& tab_container) {
  if (!tab_container.element) {
    return nullptr;
  }

  ComPtr<IUIAutomationElementArray> tab_elements;
  if (FAILED(tab_container.element->FindAll(
          GetTabElementScope(tab_container.kind),
          GetTabElementCondition(session, tab_container.kind).Get(),
          tab_elements.ReleaseAndGetAddressOf())) ||
      !tab_elements) {
    return nullptr;
  }

  return tab_elements;
}

ComPtr<IUIAutomationElement> FindTabElementAtPoint(
    const ComPtr<IUIAutomationElementArray>& tab_elements,
    POINT pt) {
  if (!tab_elements) {
    return nullptr;
  }

  int count = 0;
  if (FAILED(tab_elements->get_Length(&count))) {
    return nullptr;
  }

  for (int i = 0; i < count; ++i) {
    ComPtr<IUIAutomationElement> tab_element;
    if (FAILED(tab_elements->GetElement(
            i, tab_element.ReleaseAndGetAddressOf())) ||
        !tab_element) {
      continue;
    }

    RECT rect;
    if (FAILED(tab_element->get_CurrentBoundingRectangle(&rect))) {
      continue;
    }
    if (PtInRect(&rect, pt)) {
      return tab_element;
    }
  }

  return nullptr;
}

bool IsOnTabCloseButton(const UiaSession& session,
                        const ComPtr<IUIAutomationElement>& tab_element,
                        POINT pt) {
  if (!tab_element) {
    return false;
  }

  const auto close_button = FindFirstDescendantByClass(
      tab_element, session.class_conditions.tab_close_button);
  if (!close_button) {
    return false;
  }

  RECT close_button_rect;
  if (FAILED(close_button->get_CurrentBoundingRectangle(&close_button_rect))) {
    return false;
  }
  return PtInRect(&close_button_rect, pt) != FALSE;
}

std::optional<TabHitResult> BuildTabHitResult(const UiaSession& session,
                                              const TabContainer& tab_container,
                                              POINT pt,
                                              bool need_count,
                                              bool need_close_button) {
  const auto tab_elements = FindTabElements(session, tab_container);
  if (!tab_elements) {
    return std::nullopt;
  }

  const auto tab_element = FindTabElementAtPoint(tab_elements, pt);
  if (!tab_element) {
    return std::nullopt;
  }

  int tab_count = 0;
  if (need_count) {
    const auto raw_count =
        CountDescendantsByClassRaw(session, tab_container.element,
                                   GetTabElementClassName(tab_container.kind));
    if (!raw_count) {
      return std::nullopt;
    }
    tab_count = *raw_count;
  }

  TabHitResult hit_result;
  hit_result.tab = tab_element;
  hit_result.tab_count = need_count ? tab_count : 0;
  hit_result.on_close_button =
      need_close_button && IsOnTabCloseButton(session, tab_element, pt);
  return hit_result;
}

}

std::optional<TabHitResult> FindTabHitResult(POINT pt,
                                             bool need_count,
                                             bool need_close_button) {
  UiaSession* session = GetUiaSession();
  if (!session) {
    return std::nullopt;
  }

  const HWND hwnd = WindowFromPoint(pt);
  const HWND root = hwnd ? GetAncestor(hwnd, GA_ROOT) : nullptr;
  if (!root || !IsChromeWindow(root)) {
    return std::nullopt;
  }

  RECT region_rect;
  TabUiCache* ui = GetValidatedTabUi(session, root, &region_rect);
  if (!ui) {
    return std::nullopt;
  }

  if (!PtInRect(&region_rect, pt)) {
    return std::nullopt;
  }

  return BuildTabHitResult(*session, ui->container, pt, need_count,
                           need_close_button);
}

std::optional<int> FindTabCount(HWND hwnd) {
  UiaSession* session = GetUiaSession();
  if (!session) {
    return std::nullopt;
  }

  RECT region_rect;
  TabUiCache* ui = GetValidatedTabUi(session, hwnd, &region_rect);
  if (!ui) {
    return std::nullopt;
  }

  return CountDescendantsByClassRaw(*session, ui->container.element,
                                    GetTabElementClassName(ui->container.kind));
}
