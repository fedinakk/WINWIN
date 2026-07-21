#pragma once
// Tracks top-level user windows and lays them out on the virtual canvas.
//
// The virtual rect of each window is the source of truth; ApplyCamera()
// projects it to screen coordinates through the shared Camera and moves the
// real windows in one DeferWindowPos batch. External moves (user dragging a
// title bar, apps repositioning themselves) are detected via WinEvents and
// synced back into virtual space, so the canvas never fights the user.

#include "Common.h"
#include "Camera.h"

struct ManagedWindow {
    HWND hwnd = nullptr;
    VRect virt{};            // canvas-space rect (source of truth)
    RECT expected{};         // last screen rect we applied (readback after move)
    bool userDragging = false; // between MOVESIZESTART and MOVESIZEEND
    bool excluded = false;     // maximized/fullscreen: geometry left alone
    bool minimized = false;
    bool parked = false;       // moved offscreen while its live preview is shown
};

class WindowTracker {
public:
    void Init(HWND mainWnd, const Camera* cam, HWND canvasWnd, HWND overlayWnd);
    void Shutdown(); // unhooks WinEvents and brings every window back on screen

    void AdoptExistingWindows();      // enumerate current top-level windows
    void ApplyCamera(bool force = false);
    void ApplyOne(ManagedWindow& mw); // project a single window (used by drags)

    void OnWinEvent(DWORD event, HWND hwnd); // called on the main thread

    ManagedWindow* Find(HWND h);
    std::vector<ManagedWindow>& Windows() { return m_windows; }

    // True for windows we are allowed to manage (filters shell/tool/cloaked).
    bool IsManageable(HWND h) const;

    // Root managed window under a screen point, or null.
    ManagedWindow* HitTest(POINT screenPt);

    // Union of virtual rects of participating windows; false if empty.
    bool ContentBounds(VRect& out) const;

    // Preview support: move the real window offscreen / bring it back.
    void Park(HWND h);
    void Unpark(HWND h);

private:
    static void CALLBACK WinEventProc(HWINEVENTHOOK, DWORD event, HWND hwnd,
                                      LONG idObject, LONG idChild, DWORD, DWORD);
    void Adopt(HWND h);
    void Remove(HWND h);
    void SyncFromScreen(ManagedWindow& mw); // screen rect -> virtual rect
    bool UpdateExcluded(ManagedWindow& mw); // maximized/fullscreen check

    HWND m_mainWnd = nullptr;
    HWND m_canvasWnd = nullptr;
    HWND m_overlayWnd = nullptr;
    const Camera* m_cam = nullptr;
    std::vector<ManagedWindow> m_windows;
    std::vector<HWINEVENTHOOK> m_hooks;
};
