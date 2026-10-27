# PrismaUI - origin and how to re-vendor

One third-party header. **Not authored by this project, and not edited by it.**
The licence ships with it: `LICENSE.md`, next to this file.

## Where it came from

| | |
|---|---|
| File | `PrismaUI_API.h`, the C++ API header of Prisma UI |
| Copied from | `src/PrismaUI_API.h` of `github.com/PrismaUI-SKSE/framework`, tag `1.5.1` (commit `aaef071`) - the framework's own repository, at the version this project was measured against |
| Same bytes as | `src/PrismaUI_API.h` of `github.com/PrismaUI-SKSE/example-skse-plugin`, commit `dbb1dd3` |
| Copied on | 2026-10-27 |
| Hash | sha256, first 16 hex: `c359a4b0b20da6de` (5992 bytes, UTF-8 with BOM) |
| Licence | Prisma UI License (`LICENSE.md`, same tag; sha256 first 16 hex `c39febb60f37fb82` as downloaded with CRLF, `d0c465225b74ce3f` with line endings normalised to LF, which is how this repository stores and checks out `.md`) |

The installed mod ships no header, so the framework's repository is the copy
of record. The header says at its top that modders should copy it into their
own project. The licence allows the Original Version to be shared unmodified as
long as the licence travels with it, unmodified - which is why both files are
here and why neither may be edited. Prisma UI itself is **not redistributed**:
this project ships only its own DLL.

## Not edited, and how it compiles anyway

The header calls `GetModuleHandle(L"PrismaUI.dll")`, which only compiles with
`UNICODE` defined. This project builds without it. Instead of touching the
header, the one file that includes it (`src/Panel/PrismaBackend.cpp`) maps
`GetModuleHandle` to `GetModuleHandleW` right before the include.

## Do not take it from another project

Other projects in the same workspace carry older copies (1.4.0). The check is
mechanical: the `InterfaceVersion` enum must list `V2`, and the hash above must
match.

## Re-vendoring

Download `src/PrismaUI_API.h` and `LICENSE.md` from the framework tag that
matches the installed Prisma UI, compare, and update the table.
