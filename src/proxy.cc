#include "proxy.h"

#include <MinHook.h>

#include <cstdint>
#include <string>

#define VERSION_STUB(name)                                      \
  extern "C" __attribute__((noinline, optnone)) uintptr_t name() { \
    __asm__ __volatile__("nop; nop; nop; nop; nop; nop; nop; nop;" \
                         "nop; nop; nop; nop; nop; nop; nop; nop;"); \
    return 0;                                                    \
  }

VERSION_STUB(StubGetFileVersionInfoA)
VERSION_STUB(StubGetFileVersionInfoByHandle)
VERSION_STUB(StubGetFileVersionInfoExA)
VERSION_STUB(StubGetFileVersionInfoExW)
VERSION_STUB(StubGetFileVersionInfoSizeA)
VERSION_STUB(StubGetFileVersionInfoSizeExA)
VERSION_STUB(StubGetFileVersionInfoSizeExW)
VERSION_STUB(StubGetFileVersionInfoSizeW)
VERSION_STUB(StubGetFileVersionInfoW)
VERSION_STUB(StubVerFindFileA)
VERSION_STUB(StubVerFindFileW)
VERSION_STUB(StubVerInstallFileA)
VERSION_STUB(StubVerInstallFileW)
VERSION_STUB(StubVerLanguageNameA)
VERSION_STUB(StubVerLanguageNameW)
VERSION_STUB(StubVerQueryValueA)
VERSION_STUB(StubVerQueryValueW)

bool PrepareVersionProxy(HMODULE self) {
  wchar_t directory[MAX_PATH + 1]{};
  if (!GetSystemDirectoryW(directory, MAX_PATH)) return false;
  std::wstring path = std::wstring(directory) + L"\\version.dll";
  const HMODULE system_version = LoadLibraryW(path.c_str());
  if (!system_version) return false;

  auto* base = reinterpret_cast<unsigned char*>(self);
  auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
  if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
  auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
  if (nt->Signature != IMAGE_NT_SIGNATURE) return false;
  const DWORD export_rva =
      nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT]
          .VirtualAddress;
  if (!export_rva) return false;
  auto* exports = reinterpret_cast<IMAGE_EXPORT_DIRECTORY*>(base + export_rva);
  auto* names = reinterpret_cast<DWORD*>(base + exports->AddressOfNames);
  auto* functions =
      reinterpret_cast<DWORD*>(base + exports->AddressOfFunctions);
  auto* ordinals = reinterpret_cast<WORD*>(base + exports->AddressOfNameOrdinals);

  for (DWORD index = 0; index < exports->NumberOfNames; ++index) {
    const char* name = reinterpret_cast<char*>(base + names[index]);
    const FARPROC original = GetProcAddress(system_version, name);
    if (!original) continue;
    void* stub = base + functions[ordinals[index]];
    if (MH_CreateHook(stub, reinterpret_cast<void*>(original), nullptr) !=
        MH_OK) {
      return false;
    }
  }
  return true;
}
