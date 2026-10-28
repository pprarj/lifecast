# MagelightUI - origin and how to re-vendor

One third-party header. **Not authored by this project, and not edited by it.**
The licence ships with it: `LICENSE.txt`, next to this file.

## Where it came from

| | |
|---|---|
| File | `MagelightUI_API.h`, the C++ API header of Magelight UI |
| Copied from | `api/MagelightUI_API.h` of `github.com/Severause/MagelightUI`, tag `v1.5.1` (commit `9acebb2fa8422f6b0f9a2bc4b175349e91b2bd68`) - the framework's own repository, at the version this project was measured against |
| Copied on | 2026-10-27 |
| Hash | sha256, first 16 hex: `dd74ee47dc3778ba` as downloaded (59887 bytes, CRLF); `92a89294869599b4` with line endings normalised to LF, which is how this repository stores and checks out `.h` |
| Licence | MIT, "Copyright (c) 2026 Severause" - the `LICENSE` of the same tag, saved as `LICENSE.txt` (sha256 first 16 hex `8e5573b3dd382dea` as downloaded with CRLF, `1d3bc073f6ccb5f7` normalised to LF) |

The installed mod ships no header, so the framework's repository is the copy
of record. The comments of the header carry a few non-ASCII characters (dashes,
arrows): they are the third party's text and are left as they are.

Magelight UI itself is **not redistributed**: this project ships only its own
DLL, and the panel's page in the folder Magelight serves.

## What is not here

`api/MagelightUI_Mod.h`, a convenience wrapper in the same repository, was read
for the order of registration and calls and is not copied: nothing from it is
in this project.

## Do not take it from another copy

The header is versioned by its `hostVersionNumber` gates. The check is
mechanical: the `MagelightApi4` struct must end with `CancelUIModeRequest`
(1.1.0), and the hash above must match.

## Re-vendoring

Download `api/MagelightUI_API.h` and `LICENSE` from the framework tag that
matches the installed Magelight UI, compare, and update the table.
