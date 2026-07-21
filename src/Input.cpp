#include "Input.h"
#include "Config.h"

namespace {

HHOOK g_hook = nullptr;
HWND g_main = nullptr;
HWND g_canvas = nullptr;
HWND g_overlay = nullptr;

enum class Mode { None, Pan, Drag, Resize };
Mode g_mode = Mode::None;

bool KeyDown(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

// Cheap synchronous filter: is there a plausibly draggable window under the
// cursor? The precise "is it managed" answer is computed on the main thread;
// this only has to avoid swallowing clicks over the shell and our own windows.
bool DraggableUnder(POINT pt) {
    HWND h = WindowFromPoint(pt);
    if (!h) return false;
    HWND root = GetAncestor(h, GA_ROOT);
    if (!root || root == g_canvas || root == g_main || root == g_overlay) return false;
    wchar_t cls[64]{};
    GetClassNameW(root, cls, 64);
    const wchar_t* shell[] = { L"Shell_TrayWnd", L"Shell_SecondaryTrayWnd",
                               L"Progman", L"WorkerW", L"NotifyIconOverflowWindow" };
    for (auto* s : shell)
        if (wcscmp(cls, s) == 0) return false;
    return true;
}

} // namespace

LRESULT CALLBACK InputHook::MouseProc(int code, WPARAM wp, LPARAM lp) {
    if (code != HC_ACTION || !g_main)
        return CallNextHookEx(g_hook, code, wp, lp);

    const auto* ms = (const MSLLHOOKSTRUCT*)lp;
    const POINT pt = ms->pt;
    const bool ctrl = KeyDown(VK_CONTROL);
    const bool alt = KeyDown(VK_MENU);
    const bool shift = KeyDown(VK_SHIFT);

    switch (wp) {
    case WM_MOUSEWHEEL:
        if (ctrl && alt) {
            short delta = (short)HIWORD(ms->mouseData);
            PostMessageW(g_main, IDM_WM_ZOOM, (WPARAM)(INT_PTR)delta, PointToLParam(pt));
            return 1; // swallow: the app under the cursor must not scroll
        }
        break;

    case WM_MBUTTONDOWN:
        if (ctrl && alt && g_mode == Mode::None) {
            g_mode = Mode::Pan;
            PostMessageW(g_main, IDM_WM_PAN_BEGIN, 0, PointToLParam(pt));
            return 1;
        }
        break;

    case WM_MBUTTONUP:
        if (g_mode == Mode::Pan) {
            g_mode = Mode::None;
            PostMessageW(g_main, IDM_WM_PAN_END, 0, PointToLParam(pt));
            return 1;
        }
        break;

    case WM_LBUTTONDOWN:
        if (alt && !ctrl && g_mode == Mode::None && DraggableUnder(pt)) {
            g_mode = Mode::Drag;
            PostMessageW(g_main, IDM_WM_DRAG_BEGIN, shift ? 1 : 0, PointToLParam(pt));
            return 1;
        }
        break;

    case WM_LBUTTONUP:
        if (g_mode == Mode::Drag) {
            g_mode = Mode::None;
            PostMessageW(g_main, IDM_WM_DRAG_END, 0, PointToLParam(pt));
            return 1;
        }
        break;

    case WM_RBUTTONDOWN:
        if (alt && !ctrl && g_mode == Mode::None && DraggableUnder(pt)) {
            g_mode = Mode::Resize;
            PostMessageW(g_main, IDM_WM_RESIZE_BEGIN, shift ? 1 : 0, PointToLParam(pt));
            return 1;
        }
        break;

    case WM_RBUTTONUP:
        if (g_mode == Mode::Resize) {
            g_mode = Mode::None;
            PostMessageW(g_main, IDM_WM_RESIZE_END, 0, PointToLParam(pt));
            return 1;
        }
        break;

    case WM_MOUSEMOVE:
        // Never swallowed (that would freeze the cursor system-wide).
        switch (g_mode) {
        case Mode::Pan:    PostMessageW(g_main, IDM_WM_PAN_MOVE, 0, PointToLParam(pt)); break;
        case Mode::Drag:   PostMessageW(g_main, IDM_WM_DRAG_MOVE, 0, PointToLParam(pt)); break;
        case Mode::Resize: PostMessageW(g_main, IDM_WM_RESIZE_MOVE, 0, PointToLParam(pt)); break;
        default: break;
        }
        break;
    }

    return CallNextHookEx(g_hook, code, wp, lp);
}

bool InputHook::Install(HWND mainWnd, HWND canvasWnd, HWND overlayWnd) {
    g_main = mainWnd;
    g_canvas = canvasWnd;
    g_overlay = overlayWnd;
    g_mode = Mode::None;
    g_hook = SetWindowsHookExW(WH_MOUSE_LL, MouseProc, GetModuleHandleW(nullptr), 0);
    return g_hook != nullptr;
}

void InputHook::Uninstall() {
    if (g_hook) UnhookWindowsHookEx(g_hook);
    g_hook = nullptr;
    g_main = nullptr;
}

bool InputHook::RegisterHotkeys(HWND mainWnd, const Config& cfg) {
    struct { int id; const wchar_t* name; } map[] = {
        { HK_ZOOM_FIT, L"ZoomToFit" }, { HK_HOME, L"Home" },
        { HK_CENTER, L"CenterWindow" }, { HK_RESET_ZOOM, L"ResetZoom" },
        { HK_TOGGLE_FPS, L"ToggleFps" }, { HK_EXIT, L"Exit" },
        { HK_BOOKMARK_1, L"Bookmark1" }, { HK_BOOKMARK_2, L"Bookmark2" },
        { HK_BOOKMARK_3, L"Bookmark3" }, { HK_BOOKMARK_4, L"Bookmark4" },
        { HK_SAVE_BOOKMARK_1, L"SaveBookmark1" }, { HK_SAVE_BOOKMARK_2, L"SaveBookmark2" },
        { HK_SAVE_BOOKMARK_3, L"SaveBookmark3" }, { HK_SAVE_BOOKMARK_4, L"SaveBookmark4" },
    };
    bool allOk = true;
    for (auto& e : map) {
        auto it = cfg.hotkeys.find(e.name);
        if (it == cfg.hotkeys.end() || !it->second.Valid()) continue;
        if (!RegisterHotKey(mainWnd, e.id, it->second.modifiers | MOD_NOREPEAT, it->second.vk))
            allOk = false; // taken by another app; keep going
    }
    return allOk;
}

void InputHook::UnregisterHotkeys(HWND mainWnd) {
    for (int id = HK_ZOOM_FIT; id <= HK_SAVE_BOOKMARK_4; ++id)
        UnregisterHotKey(mainWnd, id);
}
