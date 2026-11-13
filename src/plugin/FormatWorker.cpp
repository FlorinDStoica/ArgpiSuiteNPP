#include "FormatWorker.h"

#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "ConfigResolver.h"
#include "OutputLog.h"
#include "TextUtil.h"
#include "format/Format.h"
#include "format/FormatLanguage.h"

namespace ArgpiSuite::Plugin {
namespace {

namespace fs = std::filesystem;

constexpr wchar_t kProgressClass[] = L"ArgpiSuiteFormatProgress";
constexpr UINT_PTR kPollTimer = 1;
constexpr int kIdProgress = 30300;
constexpr int kIdStatus = 30301;
constexpr int kIdCancel = 30302;

enum class FileEncoding { Utf8, Utf8Bom, Utf16Le, Utf16Be };

struct BatchContext {
    std::vector<std::wstring> files;
    Settings settings;
    std::atomic<int> done{0};
    std::atomic<int> total{0};
    std::atomic<int> formatted{0};
    std::atomic<int> unchanged{0};
    std::atomic<int> failed{0};
    std::atomic<int> skipped{0};
    std::atomic_bool cancel{false};
    std::atomic_bool finished{false};
    std::mutex logMutex;
    std::vector<std::wstring> messages;
    std::thread thread;

    void Log(std::wstring message) {
        std::lock_guard<std::mutex> lock(logMutex);
        messages.push_back(std::move(message));
    }

    std::vector<std::wstring> Drain() {
        std::lock_guard<std::mutex> lock(logMutex);
        std::vector<std::wstring> drained;
        drained.swap(messages);
        return drained;
    }
};

struct ProgressState {
    BatchContext context;
    HWND window = nullptr;
    HWND progress = nullptr;
    HWND status = nullptr;
    HWND cancelButton = nullptr;
    bool cancelRequested = false;
};

std::string ReadFileBytes(const std::wstring& path, bool& ok) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        ok = false;
        return {};
    }
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    ok = true;
    return buffer.str();
}

FileEncoding DetectEncoding(std::string_view bytes, std::size_t& bomLength) {
    bomLength = 0;
    if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xEF &&
        static_cast<unsigned char>(bytes[1]) == 0xBB &&
        static_cast<unsigned char>(bytes[2]) == 0xBF) {
        bomLength = 3;
        return FileEncoding::Utf8Bom;
    }
    if (bytes.size() >= 2 && static_cast<unsigned char>(bytes[0]) == 0xFF &&
        static_cast<unsigned char>(bytes[1]) == 0xFE) {
        bomLength = 2;
        return FileEncoding::Utf16Le;
    }
    if (bytes.size() >= 2 && static_cast<unsigned char>(bytes[0]) == 0xFE &&
        static_cast<unsigned char>(bytes[1]) == 0xFF) {
        bomLength = 2;
        return FileEncoding::Utf16Be;
    }
    return FileEncoding::Utf8;
}

std::string Utf16BytesToUtf8(std::string_view payload, bool bigEndian) {
    std::wstring wide;
    wide.reserve(payload.size() / 2);
    for (std::size_t i = 0; i + 1 < payload.size(); i += 2) {
        const unsigned char first = static_cast<unsigned char>(payload[i]);
        const unsigned char second = static_cast<unsigned char>(payload[i + 1]);
        const wchar_t unit = bigEndian
                                 ? static_cast<wchar_t>((first << 8) | second)
                                 : static_cast<wchar_t>((second << 8) | first);
        wide.push_back(unit);
    }
    return ToUtf8(wide);
}

std::string EncodeUtf16(const std::string& utf8, bool bigEndian) {
    const std::wstring wide = ToWide(utf8);
    std::string out;
    out.reserve(wide.size() * 2);
    for (wchar_t unit : wide) {
        const unsigned char low = static_cast<unsigned char>(unit & 0xFF);
        const unsigned char high = static_cast<unsigned char>((unit >> 8) & 0xFF);
        if (bigEndian) {
            out.push_back(static_cast<char>(high));
            out.push_back(static_cast<char>(low));
        } else {
            out.push_back(static_cast<char>(low));
            out.push_back(static_cast<char>(high));
        }
    }
    return out;
}

bool WriteFileAtomically(const std::wstring& path, const std::string& bytes, std::wstring& error) {
    const std::wstring temporary = path + L".argpifmt.tmp";
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream) {
            error = L"cannot create temporary file: " + temporary;
            return false;
        }
        stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        if (!stream) {
            error = L"cannot write temporary file: " + temporary;
            stream.close();
            ::DeleteFileW(temporary.c_str());
            return false;
        }
    }
    if (::ReplaceFileW(path.c_str(), temporary.c_str(), nullptr, REPLACEFILE_IGNORE_MERGE_ERRORS,
                       nullptr, nullptr) != FALSE) {
        return true;
    }
    if (::MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING) != FALSE) {
        return true;
    }
    error = L"cannot replace file: " + path;
    ::DeleteFileW(temporary.c_str());
    return false;
}

bool ShouldSkipDirectory(const std::wstring& name) {
    static const wchar_t* kSkipped[] = {L".git", L".hg", L".svn", L"node_modules", L".vs",
                                        L"bin",  L"obj", L"dist", L"build", L"target"};
    for (const wchar_t* candidate : kSkipped) {
        if (EqualsIgnoreCase(name, candidate)) {
            return true;
        }
    }
    return false;
}

std::vector<std::wstring> CollectFiles(const std::wstring& root, const Settings& settings,
                                       ConfigResolver& resolver, std::wstring& error) {
    std::vector<std::wstring> extensions;
    for (const std::wstring& item : SplitList(settings.extensionFilter)) {
        std::wstring extension = ToLower(item);
        if (!extension.empty() && extension.front() == L'.') {
            extension.erase(0, 1);
        }
        if (!extension.empty()) {
            extensions.push_back(extension);
        }
    }

    std::vector<std::wstring> files;
    std::error_code code;
    fs::recursive_directory_iterator iterator(
        fs::path(root), fs::directory_options::skip_permission_denied, code);
    if (code) {
        error = L"cannot open folder: " + root;
        return files;
    }
    const fs::recursive_directory_iterator end;
    for (; iterator != end; iterator.increment(code)) {
        if (code) {
            break;
        }
        const fs::directory_entry& entry = *iterator;
        if (entry.is_directory(code)) {
            if (ShouldSkipDirectory(entry.path().filename().wstring())) {
                iterator.disable_recursion_pending();
            }
            continue;
        }
        if (!entry.is_regular_file(code)) {
            continue;
        }
        const std::wstring path = entry.path().wstring();
        const std::wstring extension = ToLower(ExtensionOf(path));
        bool allowed = extensions.empty();
        for (const std::wstring& candidate : extensions) {
            if (extension == candidate) {
                allowed = true;
                break;
            }
        }
        if (!allowed) {
            continue;
        }
        if (resolver.IsIgnored(path)) {
            continue;
        }
        files.push_back(path);
    }
    error.clear();
    return files;
}

void ProcessFile(const std::wstring& path, BatchContext& context) {
    bool readOk = false;
    const std::string bytes = ReadFileBytes(path, readOk);
    if (!readOk) {
        ++context.failed;
        context.Log(L"failed to read: " + path);
        return;
    }

    std::size_t bomLength = 0;
    const FileEncoding encoding = DetectEncoding(bytes, bomLength);
    std::string utf8;
    if (encoding == FileEncoding::Utf16Le || encoding == FileEncoding::Utf16Be) {
        utf8 = Utf16BytesToUtf8(std::string_view(bytes).substr(bomLength),
                                encoding == FileEncoding::Utf16Be);
    } else {
        utf8 = bytes;  // FormatText handles a UTF-8 BOM
    }

    ConfigResolver resolver(context.settings);
    const ResolvedConfig config = resolver.Resolve(path);
    if (context.settings.requireConfig && !config.hasConfig) {
        ++context.skipped;
        context.Log(L"skipped (no config): " + path);
        return;
    }

    const Format::Language language = Format::DetectLanguageFromPath(ToUtf8(path));
    if (!Format::IsLanguageImplemented(language)) {
        ++context.skipped;
        return;
    }

    Format::FormatOptions options = config.options;
    options.endOfLine = Format::Eol::None;  // keep each file's own EOL
    const Format::FormatResult result = Format::FormatText(utf8, language, options,
                                                           &context.cancel);
    if (result.canceled) {
        return;
    }
    if (!result.ok) {
        ++context.failed;
        std::wstring message = L"error in " + path + L": " + ToWide(result.error.message);
        if (result.error.line > 0) {
            message += L" (" + std::to_wstring(result.error.line) + L":" +
                       std::to_wstring(result.error.column) + L")";
        }
        context.Log(std::move(message));
        return;
    }
    if (result.output == utf8) {
        ++context.unchanged;
        return;
    }

    std::string encoded;
    if (encoding == FileEncoding::Utf16Le || encoding == FileEncoding::Utf16Be) {
        std::string body = result.output;
        if (body.size() >= 3 && static_cast<unsigned char>(body[0]) == 0xEF &&
            static_cast<unsigned char>(body[1]) == 0xBB &&
            static_cast<unsigned char>(body[2]) == 0xBF) {
            body.erase(0, 3);
        }
        encoded = EncodeUtf16(body, encoding == FileEncoding::Utf16Be);
        encoded.insert(0, 1, static_cast<char>(encoding == FileEncoding::Utf16Be ? 0xFE : 0xFF));
        encoded.insert(1, 1, static_cast<char>(encoding == FileEncoding::Utf16Be ? 0xFF : 0xFE));
    } else if (encoding == FileEncoding::Utf8Bom) {
        encoded = result.output;
        if (encoded.size() < 3 || static_cast<unsigned char>(encoded[0]) != 0xEF ||
            static_cast<unsigned char>(encoded[1]) != 0xBB ||
            static_cast<unsigned char>(encoded[2]) != 0xBF) {
            encoded.insert(0, "\xEF\xBB\xBF");
        }
    } else {
        encoded = result.output;
    }

    std::wstring error;
    if (!WriteFileAtomically(path, encoded, error)) {
        ++context.failed;
        context.Log(std::move(error));
        return;
    }
    ++context.formatted;
}

void RunBatch(BatchContext& context) {
    for (const std::wstring& path : context.files) {
        if (context.cancel.load(std::memory_order_relaxed)) {
            break;
        }
        ProcessFile(path, context);
        context.done.fetch_add(1, std::memory_order_relaxed);
    }
    context.finished.store(true, std::memory_order_release);
}

std::wstring BuildSummary(const BatchContext& context, int total) {
    std::wstring summary = L"Format folder finished: ";
    summary += std::to_wstring(context.formatted.load());
    summary += L" formatted, ";
    summary += std::to_wstring(context.unchanged.load());
    summary += L" unchanged, ";
    summary += std::to_wstring(context.failed.load());
    summary += L" failed, ";
    summary += std::to_wstring(context.skipped.load());
    summary += L" skipped (" + std::to_wstring(total) + L" files)";
    if (context.cancel.load()) {
        summary += L" — canceled";
    }
    return summary;
}

void LayOut(ProgressState& state) {
    RECT client{};
    ::GetClientRect(state.window, &client);
    const int margin = 12;
    ::MoveWindow(state.status, margin, margin, client.right - margin * 2, 36, TRUE);
    ::MoveWindow(state.progress, margin, 58, client.right - margin * 2, 20, TRUE);
    ::MoveWindow(state.cancelButton, client.right - margin - 90,
                 client.bottom - margin - 28, 90, 28, TRUE);
}

LRESULT CALLBACK ProgressProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    ProgressState* state = reinterpret_cast<ProgressState*>(
        ::GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const CREATESTRUCTW* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        state = static_cast<ProgressState*>(create->lpCreateParams);
        ::SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }
    if (state == nullptr) {
        return ::DefWindowProcW(window, message, wParam, lParam);
    }

    switch (message) {
        case WM_SIZE:
            LayOut(*state);
            return 0;
        case WM_TIMER: {
            BatchContext& context = state->context;
            const int done = context.done.load();
            const int total = context.total.load();
            ::SendMessageW(state->progress, PBM_SETRANGE32, 0,
                           static_cast<LPARAM>(total > 0 ? total : 1));
            ::SendMessageW(state->progress, PBM_SETPOS, static_cast<WPARAM>(done), 0);
            wchar_t text[256] = {};
            ::swprintf_s(text, L"%d / %d files — %d formatted, %d failed", done, total,
                         context.formatted.load(), context.failed.load());
            ::SetWindowTextW(state->status, text);

            for (const std::wstring& line : context.Drain()) {
                OutputLog::Instance().AppendLine(line);
            }
            if (context.finished.load(std::memory_order_acquire)) {
                ::KillTimer(window, kPollTimer);
                if (context.thread.joinable()) {
                    context.thread.join();
                }
                OutputLog::Instance().AppendLine(BuildSummary(context, total));
                ::DestroyWindow(window);
            }
            return 0;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == kIdCancel && !state->cancelRequested) {
                state->cancelRequested = true;
                state->context.cancel.store(true);
                ::EnableWindow(state->cancelButton, FALSE);
                ::SetWindowTextW(state->status, L"Canceling…");
            }
            return 0;
        case WM_CLOSE:
            if (!state->cancelRequested) {
                state->cancelRequested = true;
                state->context.cancel.store(true);
                ::EnableWindow(state->cancelButton, FALSE);
                ::SetWindowTextW(state->status, L"Canceling…");
            }
            return 0;
        case WM_NCDESTROY: {
            ::KillTimer(window, kPollTimer);
            if (state->context.thread.joinable()) {
                state->context.cancel.store(true);
                state->context.thread.join();
            }
            ::SetWindowLongPtrW(window, GWLP_USERDATA, 0);
            delete state;
            return 0;
        }
        default:
            break;
    }
    return ::DefWindowProcW(window, message, wParam, lParam);
}

bool ShowProgress(HWND owner, ProgressState* state) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.style = CS_HREDRAW | CS_VREDRAW;
        windowClass.lpfnWndProc = &ProgressProc;
        windowClass.hInstance = ::GetModuleHandleW(nullptr);
        windowClass.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
        windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        windowClass.lpszClassName = kProgressClass;
        ::RegisterClassExW(&windowClass);
        registered = true;
    }

    RECT ownerRect{};
    if (owner != nullptr) {
        ::GetWindowRect(owner, &ownerRect);
    }
    const int width = 560;
    const int height = 170;
    const int x = ownerRect.left + (ownerRect.right - ownerRect.left - width) / 2;
    const int y = ownerRect.top + (ownerRect.bottom - ownerRect.top - height) / 2;

    state->window = ::CreateWindowExW(WS_EX_DLGMODALFRAME, kProgressClass,
                                      L"ArgpiSuite — Format Folder", WS_POPUP | WS_CAPTION,
                                      x, y, width, height, owner, nullptr,
                                      ::GetModuleHandleW(nullptr), state);
    if (state->window == nullptr) {
        return false;
    }

    state->status = ::CreateWindowExW(0, L"STATIC", L"Preparing…",
                                      WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 100, 30,
                                      state->window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdStatus)),
                                      ::GetModuleHandleW(nullptr), nullptr);
    state->progress = ::CreateWindowExW(0, PROGRESS_CLASSW, L"", WS_CHILD | WS_VISIBLE, 0, 0, 100,
                                        20, state->window,
                                        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdProgress)),
                                        ::GetModuleHandleW(nullptr), nullptr);
    state->cancelButton =
        ::CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 90,
                          28, state->window,
                          reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdCancel)),
                          ::GetModuleHandleW(nullptr), nullptr);

    const HFONT font = static_cast<HFONT>(::GetStockObject(DEFAULT_GUI_FONT));
    ::SendMessageW(state->status, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    ::SendMessageW(state->cancelButton, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    ::SendMessageW(state->progress, PBM_SETRANGE32, 0, 1);

    LayOut(*state);
    ::ShowWindow(state->window, SW_SHOWNORMAL);
    ::SetForegroundWindow(state->window);
    ::SetTimer(state->window, kPollTimer, 200, nullptr);
    return true;
}

}  // namespace

void FormatFolderInteractive(HWND owner, const Settings& settings) {
    BROWSEINFOW browse{};
    browse.hwndOwner = owner;
    browse.lpfn = nullptr;
    browse.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_USENEWUI;
    browse.lpszTitle = L"Choose the folder to format";
    PIDLIST_ABSOLUTE item = ::SHBrowseForFolderW(&browse);
    if (item == nullptr) {
        return;
    }
    wchar_t folder[MAX_PATH * 4] = {};
    const bool resolved = ::SHGetPathFromIDListW(item, folder) != FALSE;
    ::CoTaskMemFree(item);
    if (!resolved) {
        return;
    }

    ProgressState* state = new ProgressState();
    state->context.settings = settings;
    ConfigResolver resolver(settings);
    std::wstring error;
    state->context.files = CollectFiles(folder, settings, resolver, error);
    if (!error.empty()) {
        OutputLog::Instance().AppendLine(error);
        delete state;
        return;
    }
    if (state->context.files.empty()) {
        OutputLog::Instance().AppendLine(L"No matching files found in " + std::wstring(folder));
        delete state;
        return;
    }
    state->context.total.store(static_cast<int>(state->context.files.size()));

    OutputLog::Instance().AppendLine(L"Formatting " +
                                     std::to_wstring(state->context.files.size()) +
                                     L" files in " + std::wstring(folder));
    ShowProgress(owner, state);
    if (state->window == nullptr) {
        delete state;
        return;
    }
    state->context.thread = std::thread(&RunBatch, std::ref(state->context));
}

}  // namespace ArgpiSuite::Plugin
