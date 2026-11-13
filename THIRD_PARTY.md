# Third-party code

The shipped `ArgpiSuite.dll` contains **no third-party runtime code**. The
formatter, parsers, Doc IR and Doc printer are written for this project in
C++17 and only use the Microsoft C++ standard library and the Windows SDK.

## Development-time tools

| Tool | Version | License | Use |
|---|---|---|---|
| [Prettier](https://prettier.io/) | 3.x (fixtures generated with 3.8.3) | MIT | Generates `tests/fixtures/json/*/expected.*` via `npx`. Never shipped or invoked by the plugin. |
| [Node.js](https://nodejs.org/) | 18+ | MIT and others | Runs Prettier for golden generation only. Never shipped or required at runtime. |
| [CMake](https://cmake.org/) | 3.20+ | BSD-3-Clause | Optional build system for the core library and tests. |
| [Notepad++ plugin template / headers](https://github.com/notepad-plus-plus/nppPluginTemplate) | current repository | GPL-2.0 (as shipped with Notepad++) | `PluginInterface.h`, `Notepad_plus_msgs.h`, `Scintilla.h`, `menuCmdID.h` and the DockingFeature demo code. |

The repository's `license.txt` covers the Notepad++ template portions. The
Win32 headers and import libraries come from the Windows SDK and are not
redistributed in this repository.

## Prettier behaviour reference

The array-breaking heuristic (`ContainerArrayForcesBreak`) was derived from
Prettier 3.8.3's `src/language-js/print/array.js` under the MIT license. Only
the behaviour was reimplemented; no Prettier source code is included in the
DLL.
