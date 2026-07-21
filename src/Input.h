#pragma once
// Global input: WH_MOUSE_LL hook (Ctrl+Alt wheel/middle pan, Alt drags) and
// RegisterHotKey bindings from config.
//
// The hook callback does the absolute minimum: modifier checks, a cheap
// window-under-cursor filter, PostMessage to the main thread, and the
// swallow/pass decision (returning 1 eats the event globally so the app under
// the cursor never sees it). All real work happens on the main thread.
// WM_MOUSEMOVE is never swallowed - blocking it would freeze the cursor.

#include "Common.h"

enum HotkeyId : int {
    HK_ZOOM_FIT = 1, HK_HOME, HK_CENTER, HK_RESET_ZOOM, HK_TOGGLE_FPS, HK_EXIT,
    HK_BOOKMARK_1, HK_BOOKMARK_2, HK_BOOKMARK_3, HK_BOOKMARK_4,
    HK_SAVE_BOOKMARK_1, HK_SAVE_BOOKMARK_2, HK_SAVE_BOOKMARK_3, HK_SAVE_BOOKMARK_4,
};

struct Config;

class InputHook {
public:
    // canvasWnd/overlayWnd are exempt from Alt-drag capture.
    bool Install(HWND mainWnd, HWND canvasWnd, HWND overlayWnd);
    void Uninstall();

    bool RegisterHotkeys(HWND mainWnd, const Config& cfg);
    void UnregisterHotkeys(HWND mainWnd);

private:
    static LRESULT CALLBACK MouseProc(int code, WPARAM wp, LPARAM lp);
};
