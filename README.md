# Chromic

Chromic is a small Windows x64 `version.dll` for making Chrome portable.
It keeps only portable-profile support, two tab actions, configurable launch
commands, and optional browser-policy filtering.

Chromic is an independent, unofficial project. It is not affiliated with,
endorsed by, sponsored by, or maintained by Google LLC or the Google Chrome
team. Chrome and Google Chrome are trademarks of Google LLC.

## Features

- a portable user-data directory and cache directory;
- portable machine identity and encryption hooks for profile data created
  while Chromic is active;
- keeping the final tab open;
- closing a tab by double-clicking it;
- optional browser-policy filtering;
- additional Chromium command-line arguments;
- optional commands on browser startup and exit.

The default `chromic.ini` is intentionally limited to these settings:

```ini
[general]
ignore_policies=0
launch_on_exit=
launch_on_startup=
command_line=
data_dir=%app%\..\User Data
cache_dir=

[tabs]
keep_last_tab=1
double_click_close=1
```

## Building on Windows x64

Open PowerShell in the repository root and run:

```powershell
.\build.ps1
```

That one command:

1. creates `.local\chromic.key` if it does not exist;
2. reuses the same key on every later build;
3. downloads the pinned portable LLVM-MinGW toolchain when needed;
4. verifies the toolchain archive SHA-256;
5. builds `out\version.dll` and copies `out\chromic.ini`.

The `.local`, `.tools`, `build`, and `out` directories are ignored by Git.
Back up `.local\chromic.key` somewhere safe. Losing it can make encrypted data
in an existing portable profile unreadable after rebuilding the DLL. Do not
publish the key.

An existing key is never overwritten. Running either command again simply
uses it:

```powershell
.\build.ps1
.\generate-key.ps1
```

To use another key file, pass its path explicitly:

```powershell
.\build.ps1 -PortableKeyFile .\.local\another.key
```

The same path can be provided through `CHROMIC_PORTABLE_KEY_FILE`.

### Build toolchain

The build uses
[LLVM-MinGW 20260922](https://github.com/mstorsjo/llvm-mingw/releases/tag/20260922)
for Windows x64. The automatic download is
[llvm-mingw-20260922-ucrt-x86_64.zip](https://github.com/mstorsjo/llvm-mingw/releases/download/20260922/llvm-mingw-20260922-ucrt-x86_64.zip).
The archive is approximately 191 MB and occupies approximately 715 MB after
extraction. Visual Studio and the Windows SDK are not required.

The expected archive SHA-256 is:

```text
E3AD77D117A4BEA19A7A3B333341824D79A5A371004A10E25B8504E7B3047666
```

`$toolchainSha256` in `build.ps1` is this public archive checksum. It is not a
profile encryption key. Verify a manually downloaded archive with:

```powershell
Get-FileHash .\llvm-mingw-20260922-ucrt-x86_64.zip -Algorithm SHA256
```

To use an already extracted toolchain:

```powershell
.\build.ps1 -ToolchainDir C:\Tools\llvm-mingw-20260922-ucrt-x86_64
```

The production build uses size optimization, link-time optimization,
dead-code elimination, and symbol stripping. The resulting self-contained DLL
is approximately 258 KB and does not require separate LLVM C++ runtime DLLs.

## Installation and paths

Place `version.dll` and `chromic.ini` in the same directory as `chrome.exe`.
Start that `chrome.exe` normally. Chromic adds the configured portable
arguments and restarts the same executable once.

`%app%` is a Chromic placeholder. Chromic
obtains the full path of the loaded `version.dll` with `GetModuleFileNameW`,
removes the filename, and substitutes that directory for every `%app%` value.
Because `version.dll`, `chromic.ini`, and `chrome.exe` must be together, this is
also the application directory.

For this layout:

```text
C:\PortableChrome\App\chrome.exe
C:\PortableChrome\App\version.dll
C:\PortableChrome\App\chromic.ini
```

the defaults resolve as follows:

```text
%app%\..\User Data -> C:\PortableChrome\User Data
```

Setting `data_dir=none` or `cache_dir=none` disables the corresponding
automatically added argument. Normal Windows environment variables are also
expanded in configured paths and commands.

Missing or empty `cache_dir` also leaves Chrome's cache location unchanged.
Relative paths are resolved against the directory containing the DLL, never
the shortcut's working directory. The `%app%` placeholder cannot be overridden
by a Windows environment variable of the same name.

`launch_on_startup` and `launch_on_exit` execute their values through
`cmd.exe`. Separate multiple commands with `;`. Leave the values empty if this
behavior is not needed.

## Moving a profile

Move the application directory and the entire `User Data` directory together,
including `Local State`. The destination must use a DLL built with the same
portable key. If two Chrome installations use this data, configure both to use
the same user-data root. Do not connect just a profile subdirectory to a
different user-data root. Prefer the same direct path for both launches.

On the removable installation, `data_dir=%app%\..\User Data` follows the
application when its drive letter changes. On an installed Chrome, configure
`data_dir` to point to that same removable directory. The removable installation
does not look up or depend on the installed browser's profiles.

Use a non-default user-data root on every computer. Chromium's App-Bound
provider does not create new App-Bound keys when Chrome is using a non-default
user-data directory. This does not convert previously App-Bound data. See the
[Chromium support check](https://chromium.googlesource.com/chromium/src/+/refs/heads/main/chrome/browser/os_crypt/app_bound_encryption_win.cc)
and [key provider](https://chromium.googlesource.com/chromium/src/+/refs/heads/main/chrome/browser/os_crypt/app_bound_encryption_provider_win.cc).

The DLL now cancels startup if it cannot install a required portability hook
or relaunch with the configured arguments. It does not edit `Local State`,
delete keys, migrate cookies, or repair extension registrations.

Create a new profile while Chromic is active whenever possible. Passwords,
cookies, or tokens previously protected by ordinary Chrome with machine-bound
Windows DPAPI may not become portable retroactively. Extensions, bookmarks,
settings, and data written through Chromic are intended to survive moving the
profile, but compatibility with future browser versions cannot be guaranteed.

To authenticate the portable master key without changing the profile, use
PowerShell 7.4 or later:

```powershell
.\check-profile.ps1 -UserDataDirectory '<path to the complete User Data directory>'
```

Pass `-PortableKeyFile` if the DLL was built with a different key file. The
check rejects ordinary Windows-bound master keys and keys from another build.
It reports an existing App-Bound key separately; success authenticates only
the portable master key, not every database entry or Google session. No
plaintext keys or account data are printed.

Close Chrome completely before copying or unplugging the profile. Use matching
Chrome versions when alternating installations. Google may require a new login
even when local decryption works. Device-bound credentials such as Windows
Hello are outside this DLL's portability support.

## Tests

Run `tests\run.ps1` with the existing LLVM-MinGW toolchain. It compiles a test
executable using a public test-only key and does not build or install the
production DLL. Tests exercise real API detours, failure reporting, AES-GCM
authentication without host DPAPI, and path/cache configuration. A browser
test on a second Windows account/computer is still required before describing
a particular Chrome release and profile as verified portable.

## GitHub Releases

The contents of this directory must be the root of the published repository.
In particular, `.github` and `build.ps1` must be directly in the repository
root.

Before the first automated release, create this repository secret:

```text
Settings -> Secrets and variables -> Actions -> New repository secret
Name: CHROMIC_RELEASE_KEY
```

Generate a release key inside the ignored local directory:

```powershell
.\generate-key.ps1 -OutputPath .\.local\release.key
```

Copy the 64 hexadecimal characters from `.local\release.key` into the secret.
Keep a secure backup and never commit the file. The workflow reads the secret,
builds the Windows x64 DLL, records SHA-256 checksums, creates a provenance
attestation, and publishes `version.dll`, `chromic.ini`, and
`SHA256SUMS.txt`.

All releases from one repository must keep the same `CHROMIC_RELEASE_KEY`.
Changing it prevents a new DLL from decrypting profile data written by older
release DLLs. Every user of that repository's release binaries therefore uses
the same key embedded in those DLLs. The key can be extracted from a DLL and
must not be treated as protection from somebody who already has the DLL and
profile.

### Release rules

On each push to `main`, commits after the latest valid `vX.Y.Z` or `X.Y.Z` tag
are evaluated as follows:

- if no valid version tag exists, the first release is always `v0.1.0`;
- a missing or malformed previous version also falls back to `v0.1.0`;
- `BREAKING CHANGE:`, `BREAKING CHANGES:`, or a header such as `feat!:` creates
  a major release;
- `feat` creates a minor release;
- `fix` and `perf` create a patch release;
- `refactor`, `test`, `build`, `ci`, `style`, `docs`, `revert`, `chore`, and
  `wip` do not create a release by themselves;
- unknown and non-conventional commit types do not create a release;
- the highest release level in the evaluated range wins;
- a breaking change before 1.0.0 creates 1.0.0;
- after the first release, no matching release commit means no new release;
- manual workflow runs use the same rules.

| Latest tag          | Commits since tag              | Result     |
| ------------------- | ------------------------------ | ---------- |
| none                | any commits                    | `v0.1.0`   |
| malformed tags only | any commits                    | `v0.1.0`   |
| `v0.1.0`            | `fix: preserve extensions`     | `v0.1.1`   |
| `v0.4.2`            | `feat!: change profile format` | `v1.0.0`   |
| `v1.3.0`            | `perf: reduce tab lookup cost` | `v1.3.1`   |
| `v1.3.1`            | `docs: update instructions`    | no release |

## Verification and security

The build prints the SHA-256 of `version.dll`. The binary can be inspected
with the included LLVM tools:

```powershell
Get-FileHash .\out\version.dll -Algorithm SHA256
& .\.tools\llvm-mingw-20260922-ucrt-x86_64\bin\llvm-readobj.exe `
  --coff-imports --coff-exports .\out\version.dll
```

Matching hashes confirm identical output only when the source, toolchain, and
portable key are identical. A matching hash does not prove the absence of a
vulnerability or backdoor. Stronger verification requires source review and a
clean independent rebuild.

`version.dll` runs inside browser processes with the browser's privileges. The
portable hook replaces machine-bound DPAPI output with AES-256-GCM using the
build key. This enables portability but weakens the machine binding of an
unmodified browser installation.

## Disclaimer

This software is provided **as is**, without warranty of any kind. Its authors
and distributors are not liable for data loss, account loss, incompatibility,
security incidents, or any other damage. Back up the profile and test new
builds with disposable data first. Use Chromic entirely at your own risk.

Chromic is distributed under GPL-3.0-only.
MinHook is distributed under its BSD license.
