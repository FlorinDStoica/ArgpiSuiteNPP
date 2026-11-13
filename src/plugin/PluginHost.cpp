#include "PluginHost.h"

#include "Notepad_plus_msgs.h"

namespace ArgpiSuite::Plugin {

PluginHost& PluginHost::Instance() {
    static PluginHost instance;
    return instance;
}

void PluginHost::Initialize(HANDLE module, const NppData& data) {
    if (module != nullptr) {
        module_ = module;
    }
    data_ = data;
    configPath_ = DefaultIniPath(data_._nppHandle, static_cast<HMODULE>(module_));
    settings_.Load(configPath_);
    initialized_ = true;
}

void PluginHost::Shutdown() {
    settings_.Save();
    settings_ = Settings{};
    configPath_.clear();
    data_ = NppData{};
    module_ = nullptr;
    initialized_ = false;
    busy_ = false;
}

HWND PluginHost::CurrentScintilla() const {
    if (data_._nppHandle == nullptr) {
        return nullptr;
    }
    int which = -1;
    ::SendMessageW(data_._nppHandle, NPPM_GETCURRENTSCINTILLA, 0,
                   reinterpret_cast<LPARAM>(&which));
    if (which == 0) {
        return data_._scintillaMainHandle;
    }
    if (which == 1) {
        return data_._scintillaSecondHandle;
    }
    return nullptr;
}

void PluginHost::ReloadSettings() {
    if (configPath_.empty()) {
        configPath_ = DefaultIniPath(data_._nppHandle, static_cast<HMODULE>(module_));
    }
    settings_.Load(configPath_);
}

void PluginHost::SaveSettings() {
    if (settings_.path.empty()) {
        settings_.path = configPath_.empty()
                             ? DefaultIniPath(data_._nppHandle, static_cast<HMODULE>(module_))
                             : configPath_;
    }
    settings_.Save();
}

}  // namespace ArgpiSuite::Plugin
