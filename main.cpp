#include <windows.h>
#include <objidl.h>
#include <dwmapi.h>
#include <gdiplus.h>

#include <algorithm>
#include <cwctype>
#include <iostream>
#include <iomanip>
#include <functional>
#include <io.h>
#include <fcntl.h>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <chrono>
#include <vector>

using namespace Gdiplus;

struct Options {
    bool list = false;
    std::optional<size_t> index;
    std::optional<std::wstring> pid;
    std::wstring title;
    std::wstring output = L"screenshot.png";
    std::optional<SIZE> size;
};

struct WindowInfo { HWND handle; std::wstring title; std::wstring program; DWORD pid; };

static void printUsage() {
    std::wcerr << L"Usage:\n"
        L"  window-shot --list\n"
        L"  window-shot (--index N | --pid PID | --title TEXT) [--size WIDTHxHEIGHT] [--out file.png]\n\n"
        L"--index selects a row from --list; --pid accepts a PID or numeric substring;\n"
        L"--title accepts a case-insensitive partial title.\n"
        L"--size sets the complete outer window dimensions before capturing it.\n";
}

static std::optional<SIZE> parseSize(const std::wstring& text) {
    const auto x = text.find_first_of(L"xX");
    if (x == std::wstring::npos) return std::nullopt;
    try {
        int w = std::stoi(text.substr(0, x));
        int h = std::stoi(text.substr(x + 1));
        if (w > 0 && h > 0) return SIZE{w, h};
    } catch (...) {}
    return std::nullopt;
}

static bool isCandidate(HWND hwnd) {
    if (!IsWindowVisible(hwnd)) return false;
    const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    const LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if ((style & WS_CHILD) || (exStyle & WS_EX_TOOLWINDOW)) return false;
    wchar_t title[4096]{};
    return GetWindowTextW(hwnd, title, static_cast<int>(std::size(title))) > 0;
}

static std::vector<WindowInfo> windows() {
    std::vector<WindowInfo> result;
    std::function<void(HWND)> addWindow = [&](HWND hwnd) {
        if (!isCandidate(hwnd)) return;
        if (std::any_of(result.begin(), result.end(), [hwnd](const WindowInfo& window) {
                return window.handle == hwnd;
            })) return;
        wchar_t titleBuffer[4096]{};
        const int titleLength = GetWindowTextW(hwnd, titleBuffer, static_cast<int>(std::size(titleBuffer)));
        if (titleLength <= 0) return;
        std::wstring title(titleBuffer, titleLength);
        DWORD pid{};
        GetWindowThreadProcessId(hwnd, &pid);
        std::wstring program = L"unknown";
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (process) {
            wchar_t path[MAX_PATH * 4]{}; DWORD length = static_cast<DWORD>(std::size(path));
            if (QueryFullProcessImageNameW(process, 0, path, &length)) {
                std::wstring fullPath(path, length);
                const auto slash = fullPath.find_last_of(L"\\/");
                program = slash == std::wstring::npos ? fullPath : fullPath.substr(slash + 1);
            }
            CloseHandle(process);
        }
        result.push_back({hwnd, title, program, pid});
    };

    using AddWindow = std::function<void(HWND)>;
    EnumWindows([](HWND hwnd, LPARAM parameter) -> BOOL {
        (*reinterpret_cast<AddWindow*>(parameter))(hwnd);
        return TRUE;
    }, reinterpret_cast<LPARAM>(&addWindow));
    DwmFlush();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EnumWindows([](HWND hwnd, LPARAM parameter) -> BOOL {
        (*reinterpret_cast<AddWindow*>(parameter))(hwnd);
        return TRUE;
    }, reinterpret_cast<LPARAM>(&addWindow));

    return result;
}

static std::wstring lower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return s;
}

static std::vector<const WindowInfo*> orderedWindows(const std::vector<WindowInfo>& list) {
    std::vector<const WindowInfo*> ordered;
    ordered.reserve(list.size());
    for (const auto& window : list) ordered.push_back(&window);
    std::sort(ordered.begin(), ordered.end(), [](const WindowInfo* left, const WindowInfo* right) {
        const std::wstring lp = lower(left->program), rp = lower(right->program);
        if (lp != rp) return lp < rp;
        const std::wstring lt = lower(left->title), rt = lower(right->title);
        if (lt != rt) return lt < rt;
        return left->pid < right->pid;
    });
    return ordered;
}

static void printWindows(const std::vector<WindowInfo>& list) {
    if (list.empty()) { std::wcout << L"No visible windows with a title.\n"; return; }
    const auto sorted = orderedWindows(list);
    const size_t indexWidth = std::to_wstring(sorted.size()).size();
    size_t pidWidth = 3, programWidth = 7;
    for (const auto* window : sorted) {
        pidWidth = std::max(pidWidth, std::to_wstring(window->pid).size());
        programWidth = std::max(programWidth, window->program.size());
    }
    // Ignore pathological helper-class names when sizing the table.
    programWidth = std::min(programWidth, size_t{32});
    for (size_t i = 0; i < sorted.size(); ++i) {
        const auto* window = sorted[i];
        std::wcout << std::right << std::setw(static_cast<int>(indexWidth)) << (i + 1) << L"  "
                   << std::setw(static_cast<int>(pidWidth)) << window->pid << L"  "
                   << std::left << std::setw(static_cast<int>(programWidth)) << window->program << L"  "
                   << window->title << L"\n";
    }
}

static bool encoderClsid(const WCHAR* mime, CLSID* clsid) {
    UINT count{}, bytes{};
    GetImageEncodersSize(&count, &bytes);
    std::vector<BYTE> data(bytes);
    auto* encoders = reinterpret_cast<ImageCodecInfo*>(data.data());
    if (GetImageEncoders(count, bytes, encoders) != Ok) return false;
    for (UINT i = 0; i < count; ++i)
        if (wcscmp(encoders[i].MimeType, mime) == 0) { *clsid = encoders[i].Clsid; return true; }
    return false;
}

static bool capture(HWND hwnd, const std::wstring& output) {
    RECT frame{};
    // Use the actual outer window rectangle as the sole coordinate system.
    // Use the real outer window rectangle. DWM visual bounds can include
    // shadows and would produce an incorrect crop.
    if (!GetWindowRect(hwnd, &frame)) return false;
    const int width = frame.right - frame.left, height = frame.bottom - frame.top;
    if (width <= 0 || height <= 0) return false;

    HDC screen = GetDC(nullptr);
    HDC memory = CreateCompatibleDC(screen);
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height; // top-down, matching screen coordinates
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void* pixels = nullptr;
    HBITMAP bitmap = CreateDIBSection(screen, &bmi, DIB_RGB_COLORS, &pixels, nullptr, 0);
    if (!bitmap || !memory) { if (memory) DeleteDC(memory); if (bitmap) DeleteObject(bitmap); ReleaseDC(nullptr, screen); return false; }
    HGDIOBJ old = SelectObject(memory, bitmap);
    // Ask the window to render itself without copying the desktop or overlays.
    bool copied = PrintWindow(hwnd, memory, PW_RENDERFULLCONTENT) != FALSE;
    // Some classic apps (including Paint) report success but return a black
    // client bitmap. Retry with the full window renderer in that case.
    if (copied) {
        auto* probe = static_cast<BYTE*>(pixels);
        bool allBlack = true;
        for (size_t i = 0; i < static_cast<size_t>(width) * height; ++i)
            if (probe[i * 4] || probe[i * 4 + 1] || probe[i * 4 + 2]) { allBlack = false; break; }
        if (allBlack) copied = false;
    }
    if (copied) {
        auto* px = static_cast<BYTE*>(pixels);
        // PrintWindow does not provide a usable alpha channel.
        for (size_t i = 0; i < static_cast<size_t>(width) * height; ++i) px[i * 4 + 3] = 255;
        Bitmap image(width, height, width * 4, PixelFormat32bppARGB, px);
        CLSID png{};
        // Remove the measured non-client margins from the exported image:
        // 8 px on the left, right, and bottom; preserve the top edge.
        constexpr int margin = 8;
        if (width <= margin * 2 || height <= margin) {
            SelectObject(memory, old); DeleteObject(bitmap); DeleteDC(memory); ReleaseDC(nullptr, screen);
            return false;
        }
        Rect crop(margin, 0, width - margin * 2, height - margin);
        std::unique_ptr<Bitmap> cropped(image.Clone(crop, PixelFormat32bppARGB));
        const bool saved = encoderClsid(L"image/png", &png) && cropped && cropped->Save(output.c_str(), &png, nullptr) == Ok;
        SelectObject(memory, old); DeleteObject(bitmap); DeleteDC(memory); ReleaseDC(nullptr, screen);
        return saved;
    }
    SelectObject(memory, old); DeleteObject(bitmap); DeleteDC(memory); ReleaseDC(nullptr, screen);
    return false;
}

int wmain(int argc, wchar_t** argv) {
    if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
        SetProcessDPIAware();
    _setmode(_fileno(stdout), _O_U8TEXT);
    _setmode(_fileno(stderr), _O_U8TEXT);
    HDESK inputDesktop = OpenInputDesktop(0, FALSE,
        DESKTOP_READOBJECTS | DESKTOP_WRITEOBJECTS | DESKTOP_SWITCHDESKTOP);
    if (inputDesktop && !SetThreadDesktop(inputDesktop)) {
        CloseDesktop(inputDesktop);
        inputDesktop = nullptr;
    }
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::wstring arg = argv[i];
        if (arg == L"--list") o.list = true;
        else if ((arg == L"--index" || arg == L"--pid" || arg == L"--title" || arg == L"--out" || arg == L"--size") && i + 1 < argc) {
            const std::wstring value = argv[++i];
            if (arg == L"--index") {
                try { const auto n = std::stoull(value); if (n == 0) throw std::exception(); o.index = static_cast<size_t>(n); }
                catch (...) { std::wcerr << L"Invalid index: " << value << L"\n"; return 2; }
            } else if (arg == L"--pid") {
                if (value.empty() || !std::all_of(value.begin(), value.end(),
                        [](wchar_t c) { return std::iswdigit(c) != 0; })) {
                    std::wcerr << L"Invalid PID substring: " << value << L"\n"; return 2;
                }
                o.pid = value;
            } else if (arg == L"--title") o.title = value;
            else if (arg == L"--out") o.output = value;
            else if (!(o.size = parseSize(value))) { std::wcerr << L"Invalid size: " << value << L"\n"; return 2; }
        } else { printUsage(); return 2; }
    }
    GdiplusStartupInput startup; ULONG_PTR token{};
    if (GdiplusStartup(&token, &startup, nullptr) != Ok) return 1;
    auto list = windows();
    const auto refreshedList = windows();
    for (const auto& window : refreshedList)
        if (std::none_of(list.begin(), list.end(), [&](const WindowInfo& existing) { return existing.handle == window.handle; }))
            list.push_back(window);
    if (o.list) { printWindows(list); GdiplusShutdown(token); return 0; }
    if (!o.index && !o.pid && o.title.empty()) { std::wcerr << L"Error: one of --index, --pid, or --title is required. Available windows:\n"; printWindows(list); GdiplusShutdown(token); return 2; }
    std::vector<const WindowInfo*> matches;
    if (o.index) {
        const auto ordered = orderedWindows(list);
        if (*o.index >= 1 && *o.index <= ordered.size()) matches.push_back(ordered[*o.index - 1]);
    } else {
        const auto desired = lower(o.title);
        for (const auto& window : list) {
            if (o.pid && std::to_wstring(window.pid).find(*o.pid) == std::wstring::npos) continue;
            if (!o.title.empty() && lower(window.title).find(desired) == std::wstring::npos) continue;
            matches.push_back(&window);
        }
    }
    if (matches.empty()) { std::wcerr << L"No window matched the selector. Available windows:\n"; printWindows(list); GdiplusShutdown(token); return 3; }
    if (matches.size() > 1) {
        std::wcerr << L"Multiple windows match. Please provide a more specific selector:\n";
        std::vector<WindowInfo> matchingWindows;
        matchingWindows.reserve(matches.size());
        for (const auto* match : matches) matchingWindows.push_back(*match);
        printWindows(matchingWindows);
        GdiplusShutdown(token);
        return 5;
    }
    const WindowInfo& selected = *matches.front();
    if (IsIconic(selected.handle)) ShowWindow(selected.handle, SW_RESTORE);
    if (o.size) {
        // --size describes the final PNG dimensions. The capture removes an
        // 8 px left/right/bottom margin, so resize the outer window accordingly.
        constexpr int outputMargin = 8;
        const int targetOuterWidth = o.size->cx + outputMargin * 2;
        const int targetOuterHeight = o.size->cy + outputMargin;
        RECT windowRect{};
        GetWindowRect(selected.handle, &windowRect);
        const int windowWidth = windowRect.right - windowRect.left;
        const int windowHeight = windowRect.bottom - windowRect.top;
        if (windowWidth == targetOuterWidth && windowHeight == targetOuterHeight) {
            // Keep the existing window untouched when it already has the requested size.
        } else {
        SetWindowPos(selected.handle, nullptr, 0, 0, targetOuterWidth, targetOuterHeight,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        // Correct for any DWM/DPI rounding so the actual outer size is exact.
        for (int attempt = 0; attempt < 3; ++attempt) {
            RECT actualOuter{};
            GetWindowRect(selected.handle, &actualOuter);
            const int actualWidth = actualOuter.right - actualOuter.left;
            const int actualHeight = actualOuter.bottom - actualOuter.top;
            const int deltaWidth = targetOuterWidth - actualWidth;
            const int deltaHeight = targetOuterHeight - actualHeight;
            if (deltaWidth == 0 && deltaHeight == 0) break;
            SetWindowPos(selected.handle, nullptr, 0, 0,
                         targetOuterWidth - deltaWidth,
                         targetOuterHeight - deltaHeight,
                         SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        }
        // Force layout/paint, flush the compositor, then allow the target app
        // time to finish rendering its content at the new size.
        RedrawWindow(selected.handle, nullptr, nullptr,
                     RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN | RDW_UPDATENOW);
        DwmFlush();
        std::this_thread::sleep_for(std::chrono::seconds(1));
        DwmFlush();
        }
    }
    if (!capture(selected.handle, o.output)) { std::wcerr << L"Failed to capture or save the PNG.\n"; GdiplusShutdown(token); return 4; }
    std::wcout << L"Capture saved to: " << o.output << L"\n";
    GdiplusShutdown(token);
    if (inputDesktop) CloseDesktop(inputDesktop);
    return 0;
}


