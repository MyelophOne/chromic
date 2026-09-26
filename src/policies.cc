#include "policies.h"

#include <windows.h>
#include <shlwapi.h>

#include <MinHook.h>

#include "config.h"

namespace {

using RegOpenKeyExWFn = LSTATUS(WINAPI*)(HKEY, LPCWSTR, DWORD, REGSAM, PHKEY);
RegOpenKeyExWFn g_original = nullptr;
void* g_target = nullptr;

LSTATUS WINAPI FilteredRegOpenKeyExW(HKEY key, LPCWSTR subkey, DWORD options,
                                     REGSAM access, PHKEY result) {
  const bool root = key == HKEY_LOCAL_MACHINE || key == HKEY_CURRENT_USER;
  const bool policy = subkey &&
      (StrStrIW(subkey, L"Policies\\Google\\Chrome") ||
       StrStrIW(subkey, L"Policies\\Microsoft\\Edge") ||
       StrStrIW(subkey, L"Policies\\Chromium") ||
       StrStrIW(subkey, L"Policies\\BraveSoftware\\Brave"));
  if (root && policy) return ERROR_FILE_NOT_FOUND;
  return g_original(key, subkey, options, access, result);
}

}

void InstallPolicyHook() {
  if (!GetConfig().ignore_policies()) return;
  if (MH_CreateHookApiEx(L"advapi32", "RegOpenKeyExW",
                         reinterpret_cast<void*>(FilteredRegOpenKeyExW),
                         reinterpret_cast<void**>(&g_original), &g_target) ==
      MH_OK) {
    MH_EnableHook(g_target);
  }
}
