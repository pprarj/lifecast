# SKSEMenuFramework - origin and how to re-vendor

One third-party header. **Not authored by this project, and not edited by it.**
The licences ship with it: `LICENSE.txt` (GPL-3.0, the framework's) and
`imgui_LICENSE.txt` (MIT, Dear ImGui's), next to this file.

## Where it came from

| | |
|---|---|
| File | `SKSEMenuFramework.h`, the public C++ API header of SKSE Menu Framework 3 (11303 lines) |
| Copied from | `resources/SKSEMenuFramework.h` of `github.com/QTR-Modding/SKSE-Menu-Framework-3`, commit `2b23ee277a40dd0f079b5885825be48b9e1a1c95` (2026-09-17). The repository has no tags; the header's last change is `6ceab0a7` (2026-09-17, "Added a key picker to the api"), older than the DLL this project was measured against and unchanged since |
| Measured against | `SKSEMenuFramework.dll` of the Nexus 3.18 upload (sha256, first 16 hex: `0c107ad4c471c904`), which reports itself as 3.14.0 at run time |
| Copied on | 2026-10-28 |
| Hash | sha256, first 16 hex: `03a39ccef309d234` (522939 bytes, LF, ASCII) |
| Licence | GPL-3.0 - the `LICENSE` at the root of the same commit, saved as `LICENSE.txt` (sha256 first 16 hex `3972dc9744f6499f`) |
| Dear ImGui | The header declares the Dear ImGui 1.90.8 API (namespace `ImGuiMCP`) and resolves every function from the framework's own DLL at run time, by name. No ImGui is linked or compiled here. Dear ImGui is MIT ("Copyright (c) 2014-2024 Omar Cornut"): `imgui/LICENSE.txt` of the same commit, saved as `imgui_LICENSE.txt` (sha256 first 16 hex `0ebf8feb061536bd`) |

The installed mod ships no header, so the framework's repository is the copy
of record.

## Licence fit

This project is GPL-3.0-or-later. A GPL-3.0 header included in it is
compatible: the combined work is distributed under GPL-3.0, which the
project's licence allows, and the source of both is public. The MIT part
(Dear ImGui's declarations) is compatible with both.

SKSE Menu Framework itself is **not redistributed**: this project ships only
its own DLL, which finds the framework's DLL at run time and does nothing
with it when it is absent.

## Not used

`AddWindowWithView` is declared in the header and is NOT exported by the DLL
above; nothing here calls it.

## Do not take it from another copy

Re-vendor from the framework's own repository, at the commit that matches the
DLL being targeted, and update the table above - never from a copy kept in
another mod's source tree.
