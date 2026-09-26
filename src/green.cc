#include "green.h"

#include <windows.h>
#include <bcrypt.h>
#include <wincrypt.h>

#include <MinHook.h>

#include <array>
#include <cstring>

#include "portable_key.h"

namespace {

using CryptProtectDataFn = BOOL(WINAPI*)(DATA_BLOB*, LPCWSTR, DATA_BLOB*,
                                        PVOID, CRYPTPROTECT_PROMPTSTRUCT*,
                                        DWORD, DATA_BLOB*);
using CryptUnprotectDataFn = BOOL(WINAPI*)(DATA_BLOB*, LPWSTR*, DATA_BLOB*,
                                          PVOID, CRYPTPROTECT_PROMPTSTRUCT*,
                                          DWORD, DATA_BLOB*);
using GetVolumeInformationWFn = BOOL(WINAPI*)(
    LPCWSTR, LPWSTR, DWORD, LPDWORD, LPDWORD, LPDWORD, LPWSTR, DWORD);
using UpdateProcThreadAttributeFn = BOOL(WINAPI*)(
    LPPROC_THREAD_ATTRIBUTE_LIST, DWORD, DWORD_PTR, PVOID, SIZE_T, PVOID,
    PSIZE_T);

CryptUnprotectDataFn g_crypt_unprotect = nullptr;
GetVolumeInformationWFn g_volume_info = nullptr;
UpdateProcThreadAttributeFn g_update_attribute = nullptr;

constexpr std::array<BYTE, 8> kHeader{'C', 'H', 'R', 'M', 'A', 'E', 'S', 1};
constexpr ULONG kNonceSize = 12;
constexpr ULONG kTagSize = 16;

struct AesHandles {
  BCRYPT_ALG_HANDLE algorithm = nullptr;
  BCRYPT_KEY_HANDLE key = nullptr;

  AesHandles() {
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_AES_ALGORITHM, nullptr,
                                    0) < 0 ||
        BCryptSetProperty(
            algorithm, BCRYPT_CHAINING_MODE,
            reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(
                BCRYPT_CHAIN_MODE_GCM)),
            sizeof(BCRYPT_CHAIN_MODE_GCM), 0) < 0 ||
        BCryptGenerateSymmetricKey(algorithm, &key, nullptr, 0,
                                   const_cast<PUCHAR>(kPortableKey),
                                   sizeof(kPortableKey), 0) < 0) {
      if (key) BCryptDestroyKey(key);
      if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
      key = nullptr;
      algorithm = nullptr;
    }
  }

  ~AesHandles() {
    if (key) BCryptDestroyKey(key);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
  }
};

bool IsChromicBlob(const DATA_BLOB* blob) {
  return blob && blob->pbData &&
         blob->cbData >= kHeader.size() + kNonceSize + kTagSize &&
         std::memcmp(blob->pbData, kHeader.data(), kHeader.size()) == 0;
}

bool EncryptPortable(const DATA_BLOB* input, DATA_BLOB* output) {
  if (!input || !output || (!input->pbData && input->cbData)) return false;
  AesHandles aes;
  if (!aes.key) return false;

  const ULONG overhead = kHeader.size() + kNonceSize + kTagSize;
  if (input->cbData > MAXDWORD - overhead) return false;
  output->cbData = input->cbData + overhead;
  output->pbData = static_cast<BYTE*>(LocalAlloc(LMEM_FIXED, output->cbData));
  if (!output->pbData) return false;

  BYTE* nonce = output->pbData + kHeader.size();
  BYTE* tag = nonce + kNonceSize;
  BYTE* ciphertext = tag + kTagSize;
  std::memcpy(output->pbData, kHeader.data(), kHeader.size());
  if (BCryptGenRandom(nullptr, nonce, kNonceSize,
                      BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) {
    LocalFree(output->pbData);
    output->pbData = nullptr;
    output->cbData = 0;
    return false;
  }

  BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO auth;
  BCRYPT_INIT_AUTH_MODE_INFO(auth);
  auth.pbNonce = nonce;
  auth.cbNonce = kNonceSize;
  auth.pbAuthData = const_cast<PUCHAR>(kHeader.data());
  auth.cbAuthData = kHeader.size();
  auth.pbTag = tag;
  auth.cbTag = kTagSize;
  ULONG written = 0;
  const NTSTATUS status = BCryptEncrypt(
      aes.key, input->pbData, input->cbData, &auth, nullptr, 0, ciphertext,
      input->cbData, &written, 0);
  if (status < 0 || written != input->cbData) {
    LocalFree(output->pbData);
    output->pbData = nullptr;
    output->cbData = 0;
    return false;
  }
  return true;
}

bool DecryptPortable(const DATA_BLOB* input, DATA_BLOB* output) {
  if (!IsChromicBlob(input) || !output) return false;
  AesHandles aes;
  if (!aes.key) return false;

  const ULONG overhead = kHeader.size() + kNonceSize + kTagSize;
  const ULONG plaintext_size = input->cbData - overhead;
  BYTE* nonce = input->pbData + kHeader.size();
  BYTE* tag = nonce + kNonceSize;
  BYTE* ciphertext = tag + kTagSize;
  BYTE* plaintext = static_cast<BYTE*>(LocalAlloc(LMEM_FIXED, plaintext_size));
  if (!plaintext && plaintext_size) return false;

  BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO auth;
  BCRYPT_INIT_AUTH_MODE_INFO(auth);
  auth.pbNonce = nonce;
  auth.cbNonce = kNonceSize;
  auth.pbAuthData = const_cast<PUCHAR>(kHeader.data());
  auth.cbAuthData = kHeader.size();
  auth.pbTag = tag;
  auth.cbTag = kTagSize;
  ULONG written = 0;
  const NTSTATUS status = BCryptDecrypt(
      aes.key, ciphertext, plaintext_size, &auth, nullptr, 0, plaintext,
      plaintext_size, &written, 0);
  if (status < 0 || written != plaintext_size) {
    LocalFree(plaintext);
    SetLastError(ERROR_INVALID_DATA);
    return false;
  }
  output->pbData = plaintext;
  output->cbData = plaintext_size;
  return true;
}

BOOL WINAPI PortableComputerName(LPWSTR, LPDWORD) {
  SetLastError(ERROR_INVALID_FUNCTION);
  return FALSE;
}

BOOL WINAPI PortableVolumeInformation(LPCWSTR root, LPWSTR volume,
                                      DWORD volume_size, LPDWORD serial,
                                      LPDWORD max_component,
                                      LPDWORD file_system_flags,
                                      LPWSTR file_system,
                                      DWORD file_system_size) {
  if (serial) {
    SetLastError(ERROR_INVALID_FUNCTION);
    return FALSE;
  }
  return g_volume_info(root, volume, volume_size, serial, max_component,
                       file_system_flags, file_system, file_system_size);
}

BOOL WINAPI PortableCryptProtect(DATA_BLOB* input, LPCWSTR, DATA_BLOB*, PVOID,
                                 CRYPTPROTECT_PROMPTSTRUCT*, DWORD,
                                 DATA_BLOB* output) {
  return EncryptPortable(input, output) ? TRUE : FALSE;
}

BOOL WINAPI PortableCryptUnprotect(DATA_BLOB* input, LPWSTR* description,
                                   DATA_BLOB* entropy, PVOID reserved,
                                   CRYPTPROTECT_PROMPTSTRUCT* prompt,
                                   DWORD flags, DATA_BLOB* output) {
  if (IsChromicBlob(input)) {
    if (description) *description = nullptr;
    return DecryptPortable(input, output) ? TRUE : FALSE;
  }

  return g_crypt_unprotect(input, description, entropy, reserved, prompt,
                           flags, output);
}

BOOL WINAPI PortableUpdateAttribute(LPPROC_THREAD_ATTRIBUTE_LIST list,
                                    DWORD flags, DWORD_PTR attribute,
                                    PVOID value, SIZE_T size, PVOID previous,
                                    PSIZE_T return_size) {
  constexpr DWORD64 kBlockNonMicrosoftBinariesAlwaysOn = 1ULL << 44;
  if (attribute == PROC_THREAD_ATTRIBUTE_MITIGATION_POLICY && value &&
      size >= sizeof(DWORD64)) {
    static_cast<DWORD64*>(value)[0] &= ~kBlockNonMicrosoftBinariesAlwaysOn;
  }
  return g_update_attribute(list, flags, attribute, value, size, previous,
                            return_size);
}

bool AddApiHook(LPCWSTR module, LPCSTR name, void* replacement,
                void** original) {
  void* target = nullptr;
  if (MH_CreateHookApiEx(module, name, replacement, original, &target) !=
      MH_OK) {
    return false;
  }
  return MH_EnableHook(target) == MH_OK;
}

}

void InstallPortableHooks() {
  void* ignored = nullptr;
  AddApiHook(L"kernel32", "GetComputerNameW",
             reinterpret_cast<void*>(PortableComputerName), &ignored);
  AddApiHook(L"kernel32", "GetVolumeInformationW",
             reinterpret_cast<void*>(PortableVolumeInformation),
             reinterpret_cast<void**>(&g_volume_info));
  AddApiHook(L"kernel32", "UpdateProcThreadAttribute",
             reinterpret_cast<void*>(PortableUpdateAttribute),
             reinterpret_cast<void**>(&g_update_attribute));
  AddApiHook(L"crypt32", "CryptProtectData",
             reinterpret_cast<void*>(PortableCryptProtect), &ignored);
  AddApiHook(L"crypt32", "CryptUnprotectData",
             reinterpret_cast<void*>(PortableCryptUnprotect),
             reinterpret_cast<void**>(&g_crypt_unprotect));
}
