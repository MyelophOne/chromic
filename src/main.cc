#include <windows.h>

#include <MinHook.h>

#include <cwchar>

#include "config.h"
#include "green.h"
#include "policies.h"
#include "portable.h"
#include "proxy.h"
#include "tabs.h"
#include "utils.h"

namespace {

using EntryPoint = int (*)();
EntryPoint g_original_entry = nullptr;

int BrowserEntry() {
  const wchar_t* command_line = GetCommandLineW();
  if (!wcsstr(command_line, L"--type=")) {
    RelaunchPortable();
    InstallPortableHooks();
    InstallPolicyHook();
    InstallTabHooks();
    RunConfiguredCommands(GetConfig().launch_on_startup());
  }

  const int result = g_original_entry();
  if (!wcsstr(command_line, L"--type=")) {
    RunConfiguredCommands(GetConfig().launch_on_exit());
  }
  return result;
}

bool PrepareEntryHook() {
  auto* base = reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
  auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
  if (!base || dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
  auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
  if (nt->Signature != IMAGE_NT_SIGNATURE) return false;
  void* entry = base + nt->OptionalHeader.AddressOfEntryPoint;
  return MH_CreateHook(entry, reinterpret_cast<void*>(BrowserEntry),
                       reinterpret_cast<void**>(&g_original_entry)) == MH_OK;
}

}

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
  if (reason != DLL_PROCESS_ATTACH) return TRUE;
  DisableThreadLibraryCalls(module);
  g_module = module;
  if (MH_Initialize() != MH_OK || !PrepareVersionProxy(module) ||
      !PrepareEntryHook() || MH_EnableHook(MH_ALL_HOOKS) != MH_OK) {
    return FALSE;
  }
  return TRUE;
}
