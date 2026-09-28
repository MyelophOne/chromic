#include "../src/green.cc"
#include "config.h"
#include "utils.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {
void Check(bool ok, const char* message) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s (Windows error %lu)\n", message, GetLastError());
    std::exit(1);
  }
}
void Release(DATA_BLOB& blob) {
  if (blob.pbData) {
    SecureZeroMemory(blob.pbData, blob.cbData);
    LocalFree(blob.pbData);
  }
  blob = {};
}
BOOL WINAPI NoMachineDpapi(DATA_BLOB*, LPWSTR*, DATA_BLOB*, PVOID,
                           CRYPTPROTECT_PROMPTSTRUCT*, DWORD, DATA_BLOB*) {
  std::fputs("FAIL: portable decryption attempted machine DPAPI\n", stderr);
  std::exit(1);
}
}

int main() {
  g_module = GetModuleHandleW(nullptr);
  const auto base = AppDir();
  wchar_t windows_directory[MAX_PATH]{};
  Check(GetWindowsDirectoryW(windows_directory, MAX_PATH) != 0, "get Windows directory");
  Check(SetCurrentDirectoryW(windows_directory) != FALSE, "change launch directory");
  Check(ExpandPath(L".\\User Data") == base + L"\\User Data",
        "relative paths use the binary directory, not the working directory");
  SetEnvironmentVariableW(L"app", L"Z:\\unrelated-environment-value");
  Check(ExpandPath(L"%app%\\User Data") == base + L"\\User Data",
        "app placeholder cannot be overridden by an environment variable");
  for (const wchar_t* value : {L"", L"none"}) {
    Check(WritePrivateProfileStringW(L"general", L"cache_dir", value,
                                     IniPath().c_str()) != FALSE, "write test INI");
    Check(!Config().cache_dir(), "empty/none cache leaves Chrome defaults");
  }
  WritePrivateProfileStringW(L"general", L"cache_dir", nullptr, IniPath().c_str());
  Check(!Config().cache_dir(), "missing cache leaves Chrome defaults");
  WritePrivateProfileStringW(L"general", L"cache_dir", L".\\Cache", IniPath().c_str());
  Check(Config().cache_dir() == base + L"\\Cache", "explicit relative cache");
  DeleteFileW(IniPath().c_str());

  Check(!InstallPortableHooks(), "hook failure must be reported before MinHook initialization");
  Check(MH_Initialize() == MH_OK, "initialize MinHook");
  Check(InstallPortableHooks(), "install all real API hooks including crypt32");
  const auto protect = reinterpret_cast<CryptProtectDataFn>(
      GetProcAddress(GetModuleHandleW(L"crypt32"), "CryptProtectData"));
  const auto unprotect = reinterpret_cast<CryptUnprotectDataFn>(
      GetProcAddress(GetModuleHandleW(L"crypt32"), "CryptUnprotectData"));
  Check(protect && unprotect, "crypt32 loaded before Chrome entry point");

  g_crypt_unprotect = NoMachineDpapi;
  for (DWORD size : {0u, 1u, 32u, 4096u}) {
    std::vector<BYTE> data(size, 0x42);
    DATA_BLOB input{size, data.data()}, encrypted{}, plain{};
    Check(protect(&input, nullptr, nullptr, nullptr, nullptr, 0, &encrypted),
          "protect through hooked Windows API");
    Check(IsChromicBlob(&encrypted), "new ciphertext uses portable format");
    Check(unprotect(&encrypted, nullptr, nullptr, nullptr, nullptr, 0, &plain),
          "decrypt without machine DPAPI");
    Check(plain.cbData == size && (!size || std::memcmp(plain.pbData, data.data(), size) == 0),
          "portable plaintext preserved");
    Release(plain);
    encrypted.pbData[kHeader.size() + kNonceSize] ^= 1;
    Check(!unprotect(&encrypted, nullptr, nullptr, nullptr, nullptr, 0, &plain),
          "modified authentication tag rejected");
    Release(encrypted);
  }
  Check(MH_Uninitialize() == MH_OK, "remove test hooks");
  std::puts("PASS: paths, cache configuration, hook failures, real API hooks, portable AES round trips and authentication");
}
