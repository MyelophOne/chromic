# Security notes

## Deliberate high-privilege behavior

Chromic is a `version.dll` proxy loaded into every Chrome process. It can do
anything that Chrome can do. Its required behavior uses API hooks for:

- the executable entry point (startup bootstrap);
- `CryptProtectData` / `CryptUnprotectData` (portable AES-GCM profile key);
- machine-name and volume-serial queries (portable identity);
- process mitigation creation (allowing this non-Microsoft DLL in children);
- `RegOpenKeyExW`, only when `ignore_policies=1`;
- current-thread mouse/keyboard messages for the two tab options.

`launch_on_startup` and `launch_on_exit` intentionally execute the local INI
contents through `cmd.exe`. Empty values disable this capability.

## What the build does online

Only `build.ps1` accesses the network. It downloads one pinned LLVM-MinGW
archive from the official GitHub release URL and verifies its SHA-256. The
compiled DLL has no WinHTTP, WinINet, URLMon, Winsock, updater, telemetry or
download code.

## Key handling

There is no shared portable-profile key in tracked sources. A local build
creates `.local/chromic.key` once and reuses it on later builds. The entire
`.local` directory and the generated `build/portable_key.h` are ignored by
Git. A different ignored key can be supplied with `-PortableKeyFile`. The
64-hex string in
`build.ps1` and `THIRD_PARTY.md` is a public toolchain checksum, not a secret.
The GitHub release workflow instead reads `CHROMIC_RELEASE_KEY` from GitHub
Actions Secrets. It deliberately reuses that repository-specific key across
releases so an updated DLL can read an existing release profile. Consequently,
all users of that release line receive DLLs containing the same extractable key.

The profile key is embedded in `version.dll` and can be recovered by someone
who has the DLL. Its purpose is authenticated portable encryption and avoiding
one global key across public builds, not resistance to a local attacker who
possesses both the DLL and profile.

## Verification performed for this tree

- Windows x64 DLL built with the documented script.
- All project C++ sources pass Clang `-Wall -Wextra -Wpedantic` syntax checks.
- AES-256-GCM round-trip succeeds and modified ciphertext is rejected.
- The DLL exports exactly the 17 names expected from Windows `version.dll`.
- Static import inspection finds no network libraries.
- Two consecutive builds with the same sources, toolchain and key produced the
  same SHA-256 after disabling PE timestamps.

These checks reduce the review surface; they are not a proof that the code or
its compiler is vulnerability-free. For stronger assurance, rebuild in a clean
VM, audit `src/` and vendored MinHook, and compare the resulting SHA-256.
