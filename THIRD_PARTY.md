# Third-party code

`third_party/minhook` is MinHook 1.3.4 from
https://github.com/TsudaKageyu/minhook/tree/v1.3.4.

Only the x64 implementation and its public header are vendored. Its BSD
license is in `third_party/minhook/LICENSE.txt`.

The build script downloads the official LLVM-MinGW 20260922 x86-64 UCRT
archive and verifies this SHA-256 before extraction:

`E3AD77D117A4BEA19A7A3B333341824D79A5A371004A10E25B8504E7B3047666`
