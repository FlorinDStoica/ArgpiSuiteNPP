// ArgpiSuite plugin — process-wide plugin state.
#pragma once

#include <windows.h>

#include <string>

#include "PluginInterface.h"
#include "Settings.h"

namespace ArgpiSuite::Plugin {

/// One instance per DLL. Holds the pointers Notepad++ hands us, the loaded
/// settings and the path of the INI file. All other plugin modules read their
/// state from here instead of keeping their own globals.
class PluginHost {
public:
    static PluginHost& Instance();

    void Initialize(HANDLE module, const NppData& data);
    /// Called from DllMain before setInfo, so the INI fallback path is correct
    /// even when Notepad++ does not answer NPPM_GETPLUGINSCONFIGDIR.
    void SetModule(HANDLE module) { module_ = module; }
    void Shutdown();

    [[nodiscard]] bool IsInitialized() const { return initialized_; }
    [[nodiscard]] HWND Npp() const { return data_._nppHandle; }
    [[nodiscard]] HWND MainScintilla() const { return data_._scintillaMainHandle; }
    [[nodiscard]] HWND SecondScintilla() const { return data_._scintillaSecondHandle; }
    [[nodiscard]] HMODULE Module() const { return static_cast<HMODULE>(module_); }

    /// Current view from NPPM_GETCURRENTSCINTILLA; nullptr when no view is open.
    [[nodiscard]] HWND CurrentScintilla() const;

    [[nodiscard]] Settings& GetSettings() { return settings_; }
    [[nodiscard]] const Settings& GetSettings() const { return settings_; }

    /// Reloads the INI file (after the settings dialog or an external edit).
    void ReloadSettings();

    /// Persists the current settings.
    void SaveSettings();

    [[nodiscard]] const std::wstring& ConfigPath() const { return configPath_; }

    /// Guard against re-entrant format-on-save (formatting triggers notifications).
    [[nodiscard]] bool IsBusy() const { return busy_; }
    void SetBusy(bool busy) { busy_ = busy; }

private:
    PluginHost() = default;

    bool initialized_ = false;
    HANDLE module_ = nullptr;
    NppData data_{};
    Settings settings_;
    std::wstring configPath_;
    bool busy_ = false;
};

}  // namespace ArgpiSuite::Plugin
