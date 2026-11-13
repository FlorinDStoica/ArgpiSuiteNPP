#include "MenuBuilder.h"

#include <windows.h>

#include <string>
#include <vector>

#include "Notepad_plus_msgs.h"
#include "TextUtil.h"

namespace ArgpiSuite::Menu {
namespace {

constexpr int kMaxMenuText = 512;

std::wstring StripMenuMnemonic(const std::wstring& text) {
    std::wstring clean;
    clean.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == L'&') {
            if (i + 1 < text.size() && text[i + 1] == L'&') {
                clean.push_back(L'&');
                ++i;
            }
            continue;
        }
        clean.push_back(text[i]);
    }
    return clean;
}

std::wstring MenuTextOf(HMENU menu, int position) {
    wchar_t buffer[kMaxMenuText] = {};
    const int length = ::GetMenuStringW(menu, static_cast<UINT>(position), buffer, kMaxMenuText,
                                        MF_BYPOSITION);
    if (length <= 0) {
        return {};
    }
    std::wstring text(buffer, static_cast<std::size_t>(length));
    // Strip an accelerator suffix that Notepad++ may have appended.
    const std::size_t tab = text.find(L'\t');
    if (tab != std::wstring::npos) {
        text.erase(tab);
    }
    return Plugin::Trim(StripMenuMnemonic(text));
}

bool PopupContainsCommand(HMENU popup, int commandId) {
    const int count = ::GetMenuItemCount(popup);
    for (int i = 0; i < count; ++i) {
        if (static_cast<int>(::GetMenuItemID(popup, static_cast<UINT>(i))) == commandId) {
            return true;
        }
        HMENU child = ::GetSubMenu(popup, i);
        if (child != nullptr && PopupContainsCommand(child, commandId)) {
            return true;
        }
    }
    return false;
}

bool PopupContainsSubmenuLabel(HMENU popup, const wchar_t* label) {
    if (popup == nullptr || label == nullptr) {
        return false;
    }
    const int count = ::GetMenuItemCount(popup);
    for (int i = 0; i < count; ++i) {
        if (::GetSubMenu(popup, i) != nullptr &&
            Plugin::EqualsIgnoreCase(MenuTextOf(popup, i), label)) {
            return true;
        }
    }
    return false;
}

int FirstValidCommandId(const FuncItem* items, int itemCount) {
    for (int i = 0; i < itemCount; ++i) {
        if (items[i]._cmdID > 0) {
            return items[i]._cmdID;
        }
    }
    return 0;
}

HMENU FindPluginPopup(HMENU plugins, const FuncItem* items, int itemCount,
                      const wchar_t* pluginLabel) {
    const int pluginCount = ::GetMenuItemCount(plugins);
    const int probeId = FirstValidCommandId(items, itemCount);

    // 1. Prefer matching by plugin label (confirmed by command ID if assigned).
    if (pluginLabel != nullptr && *pluginLabel != L'\0') {
        for (int i = 0; i < pluginCount; ++i) {
            if (!Plugin::EqualsIgnoreCase(MenuTextOf(plugins, i), pluginLabel)) {
                continue;
            }
            HMENU popup = ::GetSubMenu(plugins, i);
            if (popup == nullptr) {
                continue;
            }
            if (probeId > 0 && !PopupContainsCommand(popup, probeId)) {
                continue;
            }
            return popup;
        }
    }

    // 2. Fallback: probe every plugin popup for one of our assigned command IDs.
    if (probeId > 0) {
        for (int i = 0; i < pluginCount; ++i) {
            HMENU popup = ::GetSubMenu(plugins, i);
            if (popup != nullptr && PopupContainsCommand(popup, probeId)) {
                return popup;
            }
        }
    }

    return nullptr;
}

void CollectGroupItemIndices(const SubmenuGroup& group, std::vector<int>& out) {
    for (int i = group.firstIndex; i < group.firstIndex + group.count; ++i) {
        out.push_back(i);
    }
    if (group.extraIndex >= 0) {
        out.push_back(group.extraIndex);
    }
    auto collectChild = [&](auto& self, const ChildSubmenu& child) -> void {
        for (int i = child.firstIndex; i < child.firstIndex + child.count; ++i) {
            out.push_back(i);
        }
        if (child.extraIndex >= 0) {
            out.push_back(child.extraIndex);
        }
        if (child.children != nullptr) {
            for (int k = 0; k < child.childCount; ++k) {
                self(self, child.children[k]);
            }
        }
    };
    if (group.children != nullptr) {
        for (int k = 0; k < group.childCount; ++k) {
            collectChild(collectChild, group.children[k]);
        }
    }
}

/// Position of the first remaining item that belongs to `group`.
int FindGroupPosition(HMENU popup, const FuncItem* items, const SubmenuGroup& group) {
    std::vector<int> groupIndices;
    CollectGroupItemIndices(group, groupIndices);
    const int count = ::GetMenuItemCount(popup);
    for (int i = 0; i < count; ++i) {
        const int id = static_cast<int>(::GetMenuItemID(popup, static_cast<UINT>(i)));
        if (id <= 0) {
            continue;
        }
        for (int item : groupIndices) {
            if (items[item]._cmdID == id) {
                return i;
            }
        }
    }
    return -1;
}

void RemoveGroupItems(HMENU popup, const FuncItem* items, const SubmenuGroup& group) {
    std::vector<int> groupIndices;
    CollectGroupItemIndices(group, groupIndices);
    for (int item : groupIndices) {
        ::RemoveMenu(popup, static_cast<UINT>(items[item]._cmdID), MF_BYCOMMAND);
    }
}

bool IsMenuSeparator(HMENU menu, int position) {
    MENUITEMINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = MIIM_FTYPE;
    if (::GetMenuItemInfoW(menu, static_cast<UINT>(position), TRUE, &info) == FALSE) {
        return false;
    }
    return (info.fType & MFT_SEPARATOR) != 0;
}

void RemoveRedundantSeparators(HMENU menu) {
    int position = 0;
    int count = ::GetMenuItemCount(menu);
    while (position < count) {
        const bool isSeparator = IsMenuSeparator(menu, position);
        const bool atEdge = position == 0 || position == count - 1;
        const bool duplicate = isSeparator && position > 0 && IsMenuSeparator(menu, position - 1);
        if (isSeparator && (atEdge || duplicate)) {
            ::RemoveMenu(menu, static_cast<UINT>(position), MF_BYPOSITION);
            --count;
            continue;
        }
        ++position;
    }
}

void SetItemLabel(FuncItem& item, const std::wstring& text) {
    const std::size_t limit = static_cast<std::size_t>(menuItemSize) - 1;
    const std::wstring clipped = text.size() > limit ? text.substr(0, limit) : text;
    ::lstrcpynW(item._itemName, clipped.c_str(), menuItemSize);
}

}  // namespace

std::wstring ShortcutText(const ShortcutKey* shortcut) {
    if (shortcut == nullptr || shortcut->_key == 0) {
        return {};
    }
    std::wstring text;
    if (shortcut->_isCtrl) {
        text += L"Ctrl+";
    }
    if (shortcut->_isAlt) {
        text += L"Alt+";
    }
    if (shortcut->_isShift) {
        text += L"Shift+";
    }
    const UINT key = shortcut->_key;
    if (key >= 'A' && key <= 'Z') {
        text.push_back(static_cast<wchar_t>(key));
    } else if (key >= '0' && key <= '9') {
        text.push_back(static_cast<wchar_t>(key));
    } else if (key >= VK_F1 && key <= VK_F24) {
        text += L"F";
        text += std::to_wstring(key - VK_F1 + 1);
    } else if (key == VK_SPACE) {
        text += L"Space";
    } else {
        wchar_t name[32] = {};
        const UINT scanCode = ::MapVirtualKeyW(key, MAPVK_VK_TO_VSC);
        if (::GetKeyNameTextW(static_cast<LONG>(scanCode << 16), name, 32) > 0) {
            text += name;
        } else {
            text += L"VK";
            text += std::to_wstring(key);
        }
    }
    return text;
}

bool InstallSubmenus(HWND nppHandle, FuncItem* items, int itemCount, const wchar_t* pluginLabel,
                     const SubmenuGroup* groups, int groupCount, std::wstring& error) {
    error.clear();
    if (nppHandle == nullptr) {
        error = L"invalid Notepad++ window handle";
        return false;
    }

    HMENU plugins = reinterpret_cast<HMENU>(
        ::SendMessageW(nppHandle, NPPM_GETMENUHANDLE, NPPPLUGINMENU, 0));
    if (plugins == nullptr) {
        error = L"NPPM_GETMENUHANDLE returned no Plugins menu";
        return false;
    }

    return InstallSubmenusInMenu(plugins, nppHandle, items, itemCount, pluginLabel, groups,
                                 groupCount, error);
}

bool InstallSubmenusInMenu(HMENU plugins, HWND redrawWindow, FuncItem* items, int itemCount,
                           const wchar_t* pluginLabel, const SubmenuGroup* groups, int groupCount,
                           std::wstring& error) {
    error.clear();
    if (plugins == nullptr || items == nullptr || groups == nullptr || groupCount <= 0) {
        error = L"invalid arguments";
        return false;
    }

    HMENU pluginPopup = FindPluginPopup(plugins, items, itemCount, pluginLabel);
    if (pluginPopup == nullptr) {
        error = L"could not locate the ArgpiSuite plugin menu";
        return false;
    }

    // If submenus are already installed (e.g. repeated notification), nothing to do.
    bool allSubmenusPresent = true;
    for (int groupIndex = 0; groupIndex < groupCount; ++groupIndex) {
        if (!PopupContainsSubmenuLabel(pluginPopup, groups[groupIndex].label)) {
            allSubmenusPresent = false;
            break;
        }
    }
    if (allSubmenusPresent) {
        return true;
    }

    // Validate everything before mutating the menu.
    std::vector<int> positions(static_cast<std::size_t>(groupCount), -1);
    for (int groupIndex = 0; groupIndex < groupCount; ++groupIndex) {
        const SubmenuGroup& group = groups[groupIndex];
        std::vector<int> groupIndices;
        CollectGroupItemIndices(group, groupIndices);
        for (int item : groupIndices) {
            if (item < 0 || item >= itemCount) {
                error = L"submenu group indices are out of range";
                return false;
            }
            if (items[item]._cmdID <= 0) {
                error = L"command IDs are not assigned yet (NPPN_READY is too early?)";
                return false;
            }
        }
        positions[static_cast<std::size_t>(groupIndex)] =
            FindGroupPosition(pluginPopup, items, group);
        if (positions[static_cast<std::size_t>(groupIndex)] < 0) {
            error = std::wstring(L"command items are missing from the plugin menu for group ") +
                    group.label;
            return false;
        }
    }

    // Create all popup menus first.
    std::vector<HMENU> submenus(static_cast<std::size_t>(groupCount), nullptr);
    auto destroySubmenus = [&]() {
        for (HMENU menu : submenus) {
            if (menu != nullptr) {
                ::DestroyMenu(menu);
            }
        }
    };

    auto appendItemTo = [&](HMENU target, int item, const wchar_t* overrideLabel = nullptr) {
        std::wstring label = (overrideLabel != nullptr && *overrideLabel != L'\0')
                                 ? overrideLabel
                                 : items[item]._itemName;
        const std::wstring shortcut = ShortcutText(items[item]._pShKey);
        if (!shortcut.empty()) {
            label += L"\t";
            label += shortcut;
        }
        ::AppendMenuW(target, MF_STRING, static_cast<UINT_PTR>(items[item]._cmdID),
                      label.c_str());
    };

    auto buildChildSubmenu = [&](auto& self, const ChildSubmenu& child) -> HMENU {
        HMENU childMenu = ::CreatePopupMenu();
        if (childMenu == nullptr) {
            return nullptr;
        }
        if (child.firstIndex >= 0 && child.count > 0) {
            for (int i = 0; i < child.count; ++i) {
                const wchar_t* lbl = (child.itemLabels != nullptr) ? child.itemLabels[i] : nullptr;
                appendItemTo(childMenu, child.firstIndex + i, lbl);
            }
        }
        if (child.children != nullptr) {
            for (int k = 0; k < child.childCount; ++k) {
                const ChildSubmenu& sub = child.children[k];
                if (sub.separatorBefore && ::GetMenuItemCount(childMenu) > 0) {
                    ::AppendMenuW(childMenu, MF_SEPARATOR, 0, nullptr);
                }
                HMENU subMenu = self(self, sub);
                if (subMenu != nullptr) {
                    ::AppendMenuW(childMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(subMenu), sub.label);
                }
            }
        }
        if (child.extraIndex >= 0) {
            if (::GetMenuItemCount(childMenu) > 0) {
                ::AppendMenuW(childMenu, MF_SEPARATOR, 0, nullptr);
            }
            appendItemTo(childMenu, child.extraIndex);
        }
        return childMenu;
    };

    for (int groupIndex = 0; groupIndex < groupCount; ++groupIndex) {
        const SubmenuGroup& group = groups[groupIndex];
        HMENU submenu = ::CreatePopupMenu();
        if (submenu == nullptr) {
            error = L"CreatePopupMenu failed";
            destroySubmenus();
            return false;
        }
        for (int item = group.firstIndex; item < group.firstIndex + group.count; ++item) {
            appendItemTo(submenu, item);
        }
        if (group.children != nullptr) {
            for (int k = 0; k < group.childCount; ++k) {
                const ChildSubmenu& child = group.children[k];
                if (child.separatorBefore && ::GetMenuItemCount(submenu) > 0) {
                    ::AppendMenuW(submenu, MF_SEPARATOR, 0, nullptr);
                }
                HMENU childPopup = buildChildSubmenu(buildChildSubmenu, child);
                if (childPopup != nullptr) {
                    ::AppendMenuW(submenu, MF_POPUP, reinterpret_cast<UINT_PTR>(childPopup), child.label);
                }
            }
        }
        if (group.extraIndex >= 0) {
            if (group.children != nullptr && ::GetMenuItemCount(submenu) > 0) {
                ::AppendMenuW(submenu, MF_SEPARATOR, 0, nullptr);
            }
            appendItemTo(submenu, group.extraIndex);
        }
        submenus[static_cast<std::size_t>(groupIndex)] = submenu;
    }

    // Replace the flat items with the popups.
    for (int groupIndex = 0; groupIndex < groupCount; ++groupIndex) {
        const SubmenuGroup& group = groups[groupIndex];
        const int position = FindGroupPosition(pluginPopup, items, group);
        if (position < 0) {
            error = L"plugin menu changed while installing submenus";
            destroySubmenus();
            return false;
        }
        RemoveGroupItems(pluginPopup, items, group);

        int insertAt = position;
        if (group.separatorBefore && insertAt > 0) {
            if (::InsertMenuW(pluginPopup, static_cast<UINT>(insertAt),
                              MF_BYPOSITION | MF_SEPARATOR, 0, nullptr) == FALSE) {
                error = L"InsertMenuW failed for a separator";
                destroySubmenus();
                return false;
            }
            ++insertAt;
        }
        if (::InsertMenuW(pluginPopup, static_cast<UINT>(insertAt),
                          MF_BYPOSITION | MF_POPUP,
                          reinterpret_cast<UINT_PTR>(submenus[static_cast<std::size_t>(groupIndex)]),
                          group.label) == FALSE) {
            error = L"InsertMenuW failed for a submenu";
            destroySubmenus();
            return false;
        }
        submenus[static_cast<std::size_t>(groupIndex)] = nullptr;  // now owned by the menu
    }

    // Separate any trailing standalone items (e.g. About) from the last submenu.
    const int totalItems = ::GetMenuItemCount(pluginPopup);
    for (int pos = 1; pos < totalItems; ++pos) {
        if (::GetSubMenu(pluginPopup, pos - 1) != nullptr &&
            ::GetSubMenu(pluginPopup, pos) == nullptr &&
            (::GetMenuState(pluginPopup, static_cast<UINT>(pos), MF_BYPOSITION) & MF_SEPARATOR) ==
                0) {
            ::InsertMenuW(pluginPopup, static_cast<UINT>(pos), MF_BYPOSITION | MF_SEPARATOR, 0,
                          nullptr);
            break;
        }
    }

    RemoveRedundantSeparators(pluginPopup);
    if (redrawWindow != nullptr) {
        ::DrawMenuBar(redrawWindow);
    }
    return true;
}

void ApplyFlatPrefixes(FuncItem* items, int itemCount, const SubmenuGroup* groups, int groupCount) {
    if (items == nullptr || groups == nullptr) {
        return;
    }

    auto applyPrefix = [&](int item, const std::wstring& prefix) {
        if (item < 0 || item >= itemCount || prefix.empty()) {
            return;
        }
        const std::wstring current = items[item]._itemName;
        if (current.size() >= prefix.size() &&
            current.compare(0, prefix.size(), prefix) == 0) {
            return;
        }
        SetItemLabel(items[item], prefix + current);
    };

    auto applyChild = [&](auto& self, const ChildSubmenu& child, const std::wstring& prefix) -> void {
        for (int i = child.firstIndex; i < child.firstIndex + child.count && i < itemCount; ++i) {
            applyPrefix(i, prefix);
        }
        if (child.extraIndex >= 0 && child.extraIndex < itemCount) {
            applyPrefix(child.extraIndex, prefix);
        }
        if (child.children != nullptr) {
            for (int k = 0; k < child.childCount; ++k) {
                self(self, child.children[k], prefix);
            }
        }
    };

    for (int groupIndex = 0; groupIndex < groupCount; ++groupIndex) {
        const SubmenuGroup& group = groups[groupIndex];
        if (group.flatPrefix == nullptr) {
            continue;
        }
        const std::wstring prefix = group.flatPrefix;
        for (int item = group.firstIndex;
             item < group.firstIndex + group.count && item < itemCount; ++item) {
            applyPrefix(item, prefix);
        }
        if (group.children != nullptr) {
            for (int k = 0; k < group.childCount; ++k) {
                applyChild(applyChild, group.children[k], prefix);
            }
        }
        if (group.extraIndex >= 0 && group.extraIndex < itemCount) {
            applyPrefix(group.extraIndex, prefix);
        }
    }
}

}  // namespace ArgpiSuite::Menu
