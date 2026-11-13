// ArgpiSuite plugin — declarative submenu builder for plugin commands.
//
// Notepad++ has no submenu API, so the commands are registered as ordinary
// FuncItems and later moved into popup menus by command ID. If anything goes
// wrong the builder leaves the items flat (with a short prefix) and reports the
// reason, which is always better than losing commands.
#pragma once

#include <windows.h>

#include <string>

#include "PluginInterface.h"

namespace ArgpiSuite::Menu {

struct ChildSubmenu {
    const wchar_t* label = nullptr;             // popup label, e.g. L"Encoding / Decoding" or L"Base16"
    int firstIndex = -1;
    int count = 0;
    bool separatorBefore = false;
    int extraIndex = -1;
    const wchar_t* const* itemLabels = nullptr; // optional override labels, e.g. {L"Encode", L"Decode"}
    const ChildSubmenu* children = nullptr;
    int childCount = 0;
};

struct SubmenuGroup {
    const wchar_t* label = nullptr;       // popup label, e.g. L"Format"
    const wchar_t* flatPrefix = nullptr;  // fallback prefix, e.g. L"Format: "
    int firstIndex = 0;                   // index into FuncItem[]
    int count = 0;
    bool separatorBefore = false;
    int extraIndex = -1;                  // optional appended command index (keeps frozen indices)
    const ChildSubmenu* children = nullptr;
    int childCount = 0;
};

/// Installs every group as a popup inside the ArgpiSuite plugin menu.
/// Returns false and fills `error` when the menu could not be built; the caller
/// must then call `ApplyFlatPrefixes`.
bool InstallSubmenus(HWND nppHandle, FuncItem* items, int itemCount, const wchar_t* pluginLabel,
                     const SubmenuGroup* groups, int groupCount, std::wstring& error);

/// Same as `InstallSubmenus`, operating directly on `pluginsMenu` (and calling
/// `DrawMenuBar(redrawWindow)` when non-null).
bool InstallSubmenusInMenu(HMENU pluginsMenu, HWND redrawWindow, FuncItem* items, int itemCount,
                           const wchar_t* pluginLabel, const SubmenuGroup* groups, int groupCount,
                           std::wstring& error);

/// Renames the group items to "Format: …", "Crypto: …", "Password: …".
void ApplyFlatPrefixes(FuncItem* items, int itemCount, const SubmenuGroup* groups, int groupCount);

/// "Ctrl+Alt+Shift+P" for a shortcut pointer (empty when null).
std::wstring ShortcutText(const ShortcutKey* shortcut);

}  // namespace ArgpiSuite::Menu
