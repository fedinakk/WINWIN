#include "WindowTracker.h"

namespace {

WindowTracker* g_tracker = nullptr;
HWND g_eventTarget = nullptr;

const wchar_t* kBlockedClasses[] = {
    L"Shell_TrayWnd", L"Shell_SecondaryTrayWnd", L"Progman", L"WorkerW",
    L"NotifyIconOverflowWindow", L"Windows.UI.Core.CoreWindow",
    L"XamlExplorerHostIslandWindow", L"TopLevelWindowForOverflowXamlIsland",
    L"Shell_InputSwitchTopLevelWindow", L"TaskListThumbnailWnd",
    L"ForegroundStaging", L"EdgeUiInputTopWndClass", L"TaskManagerWindow",
};

bool IsCloaked(HWND h) {
    DWORD cloaked = 0;
    if (SUCCEEDED(DwmGetWindowAttribute(h, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))))
        return cloaked != 0;
    return false;
}

bool IsBlockedClass(HWND h) {
    wchar_t cls[128]{};
    GetClassNameW(h, cls, 128);
    for (auto* b : kBlockedClasses)
        if (wcscmp(cls, b) == 0) return true;
    return false;
}

} // namespace

void WindowTracker::Init(HWND mainWnd, const Camera* cam, HWND canvasWnd, HWND overlayWnd) {
    m_mainWnd = mainWnd;
    m_cam = cam;
    m_canvasWnd = canvasWnd;
    m_overlayWnd = overlayWnd;
    g_tracker = this;
    g_eventTarget = mainWnd;

    const DWORD pairs[][2] = {
        { EVENT_OBJECT_SHOW, EVENT_OBJECT_HIDE },
        { EVENT_OBJECT_DESTROY, EVENT_OBJECT_DESTROY },
        { EVENT_SYSTEM_MOVESIZESTART, EVENT_SYSTEM_MOVESIZEEND },
        { EVENT_SYSTEM_MINIMIZESTART, EVENT_SYSTEM_MINIMIZEEND },
        { EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_LOCATIONCHANGE },
    };
    for (auto& p : pairs) {
        HWINEVENTHOOK h = SetWinEventHook(p[0], p[1], nullptr, WinEventProc, 0, 0,
                                          WINEVENT_OUTOFCONTEXT);
        if (h) m_hooks.push_back(h);
    }
}

void WindowTracker::Shutdown() {
    for (auto h : m_hooks) UnhookWinEvent(h);
    m_hooks.clear();

    // Bring every managed window back into the visible area. Windows that
    // ended up far off-canvas are cascaded onto the primary work area.
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    RECT vs = GetVirtualScreenRect();
    int cascade = 0;
    for (auto& mw : m_windows) {
        if (!IsWindow(mw.hwnd) || mw.excluded || mw.minimized) continue;
        RECT r{};
        GetWindowRect(mw.hwnd, &r);
        bool visible = r.right > vs.left + 40 && r.left < vs.right - 40 &&
                       r.bottom > vs.top + 40 && r.top < vs.bottom - 40 && !mw.parked;
        if (visible) continue;
        int w = std::max(200L, r.right - r.left);
        int h = std::max(120L, r.bottom - r.top);
        int x = work.left + 40 + (cascade % 8) * 48;
        int y = work.top + 40 + (cascade % 8) * 40;
        ++cascade;
        SetWindowPos(mw.hwnd, nullptr, x, y, w, h,
                     SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    }
    m_windows.clear();
    g_tracker = nullptr;
    g_eventTarget = nullptr;
}

void CALLBACK WindowTracker::WinEventProc(HWINEVENTHOOK, DWORD event, HWND hwnd,
                                          LONG idObject, LONG idChild, DWORD, DWORD) {
    // Runs on the main thread (WINEVENT_OUTOFCONTEXT + hook owner's queue), but
    // may fire re-entrantly during modal loops - defer all work via PostMessage.
    if (!hwnd || idObject != OBJID_WINDOW || idChild != CHILDID_SELF) return;
    if (!g_eventTarget) return;
    PostMessageW(g_eventTarget, IDM_WM_WINEVENT, event, (LPARAM)hwnd);
}

bool WindowTracker::IsManageable(HWND h) const {
    if (!IsWindow(h) || !IsWindowVisible(h)) return false;
    if (h == m_mainWnd || h == m_canvasWnd || h == m_overlayWnd) return false;
    if (GetAncestor(h, GA_ROOT) != h) return false;

    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (pid == GetCurrentProcessId()) return false;

    LONG_PTR style = GetWindowLongPtrW(h, GWL_STYLE);
    LONG_PTR ex = GetWindowLongPtrW(h, GWL_EXSTYLE);
    if (ex & WS_EX_TOOLWINDOW) return false;
    if (ex & WS_EX_TOPMOST) return false; // OSDs, taskbar, pinned utilities
    if (ex & WS_EX_NOACTIVATE) return false;
    if (!(style & WS_CAPTION) && !(ex & WS_EX_APPWINDOW)) return false;
    if (IsCloaked(h)) return false;
    if (IsBlockedClass(h)) return false;

    RECT r{};
    if (!GetWindowRect(h, &r)) return false;
    if (r.right - r.left < 40 || r.bottom - r.top < 24) return false;
    return true;
}

void WindowTracker::AdoptExistingWindows() {
    EnumWindows([](HWND h, LPARAM lp) -> BOOL {
        auto* self = (WindowTracker*)lp;
        if (self->IsManageable(h)) self->Adopt(h);
        return TRUE;
    }, (LPARAM)this);
}

void WindowTracker::Adopt(HWND h) {
    if (Find(h)) return;
    ManagedWindow mw;
    mw.hwnd = h;
    mw.minimized = IsIconic(h) != FALSE;
    UpdateExcluded(mw);
    SyncFromScreen(mw);
    m_windows.push_back(mw);
}

void WindowTracker::Remove(HWND h) {
    m_windows.erase(std::remove_if(m_windows.begin(), m_windows.end(),
        [h](const ManagedWindow& mw) { return mw.hwnd == h; }), m_windows.end());
}

ManagedWindow* WindowTracker::Find(HWND h) {
    for (auto& mw : m_windows)
        if (mw.hwnd == h) return &mw;
    return nullptr;
}

void WindowTracker::SyncFromScreen(ManagedWindow& mw) {
    RECT r{};
    if (!GetWindowRect(mw.hwnd, &r)) return;
    mw.expected = r;
    if (mw.parked) return; // parked position is not the window's real place
    mw.virt = m_cam->ScreenToVirtual(r);
}

bool WindowTracker::UpdateExcluded(ManagedWindow& mw) {
    bool was = mw.excluded;
    bool ex = IsZoomed(mw.hwnd) != FALSE;
    if (!ex) {
        // Borderless fullscreen: window rect covers its whole monitor.
        RECT r{};
        if (GetWindowRect(mw.hwnd, &r)) {
            HMONITOR mon = MonitorFromWindow(mw.hwnd, MONITOR_DEFAULTTONEAREST);
            MONITORINFO mi{ sizeof(mi) };
            if (GetMonitorInfoW(mon, &mi) && EqualRect(&r, &mi.rcMonitor)) ex = true;
        }
    }
    mw.excluded = ex;
    return was != ex;
}

void WindowTracker::ApplyCamera(bool force) {
    if (m_windows.empty()) return;
    HDWP hdwp = BeginDeferWindowPos((int)m_windows.size());
    std::vector<ManagedWindow*> moved;

    const int minW = GetSystemMetrics(SM_CXMINTRACK);
    const int minH = GetSystemMetrics(SM_CYMINTRACK);

    for (auto& mw : m_windows) {
        if (!IsWindow(mw.hwnd)) continue;
        if (mw.userDragging || mw.excluded || mw.minimized || mw.parked) continue;

        RECT t = m_cam->VirtualToScreen(mw.virt);
        int w = std::max((int)(t.right - t.left), minW);
        int h = std::max((int)(t.bottom - t.top), minH);
        RECT target{ t.left, t.top, t.left + w, t.top + h };
        if (!force && EqualRect(&target, &mw.expected)) continue;

        mw.expected = target; // provisional; corrected by readback below
        moved.push_back(&mw);
        if (hdwp) {
            hdwp = DeferWindowPos(hdwp, mw.hwnd, nullptr, target.left, target.top, w, h,
                                  SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
        }
        if (!hdwp) {
            // Defer batch broke (a window died mid-batch) - move directly.
            SetWindowPos(mw.hwnd, nullptr, target.left, target.top, w, h,
                         SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
        }
    }
    if (hdwp) EndDeferWindowPos(hdwp);

    // Readback: windows clamp themselves (WM_GETMINMAXINFO, fixed-size dialogs),
    // so remember what geometry actually stuck. That keeps the "did the user
    // move this window" check from misfiring on clamped windows.
    for (auto* mw : moved) {
        RECT r{};
        if (GetWindowRect(mw->hwnd, &r)) mw->expected = r;
    }
}

void WindowTracker::ApplyOne(ManagedWindow& mw) {
    if (!IsWindow(mw.hwnd) || mw.excluded || mw.minimized || mw.parked) return;
    const int minW = GetSystemMetrics(SM_CXMINTRACK);
    const int minH = GetSystemMetrics(SM_CYMINTRACK);
    RECT t = m_cam->VirtualToScreen(mw.virt);
    int w = std::max((int)(t.right - t.left), minW);
    int h = std::max((int)(t.bottom - t.top), minH);
    SetWindowPos(mw.hwnd, nullptr, t.left, t.top, w, h,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    RECT r{};
    if (GetWindowRect(mw.hwnd, &r)) mw.expected = r;
}

void WindowTracker::OnWinEvent(DWORD event, HWND hwnd) {
    switch (event) {
    case EVENT_OBJECT_SHOW:
        // New window opened while the canvas is panned/zoomed: adopt it at the
        // current camera so it lands where it appeared on screen.
        if (!Find(hwnd) && IsManageable(hwnd)) Adopt(hwnd);
        break;

    case EVENT_OBJECT_HIDE:
    case EVENT_OBJECT_DESTROY:
        Remove(hwnd);
        break;

    case EVENT_SYSTEM_MOVESIZESTART:
        if (auto* mw = Find(hwnd)) mw->userDragging = true;
        break;

    case EVENT_SYSTEM_MOVESIZEEND:
        if (auto* mw = Find(hwnd)) {
            mw->userDragging = false;
            UpdateExcluded(*mw);
            SyncFromScreen(*mw);
        }
        break;

    case EVENT_SYSTEM_MINIMIZESTART:
        if (auto* mw = Find(hwnd)) mw->minimized = true;
        break;

    case EVENT_SYSTEM_MINIMIZEEND:
        if (auto* mw = Find(hwnd)) {
            mw->minimized = false;
            UpdateExcluded(*mw);
            ApplyOne(*mw); // put it back at its canvas position
        }
        break;

    case EVENT_OBJECT_LOCATIONCHANGE:
        if (auto* mw = Find(hwnd)) {
            if (mw->userDragging || mw->parked) break;
            if (UpdateExcluded(*mw)) { SyncFromScreen(*mw); break; }
            RECT r{};
            if (!GetWindowRect(hwnd, &r)) break;
            if (!EqualRect(&r, &mw->expected)) {
                // The app moved/resized itself - accept it into virtual space.
                mw->expected = r;
                if (!mw->excluded && !mw->minimized) mw->virt = m_cam->ScreenToVirtual(r);
            }
        } else if (IsManageable(hwnd)) {
            Adopt(hwnd); // windows that appeared without a SHOW event
        }
        break;
    }
}

ManagedWindow* WindowTracker::HitTest(POINT pt) {
    HWND h = WindowFromPoint(pt);
    if (!h) return nullptr;
    return Find(GetAncestor(h, GA_ROOT));
}

bool WindowTracker::ContentBounds(VRect& out) const {
    bool any = false;
    double l = 0, t = 0, r = 0, b = 0;
    for (auto& mw : m_windows) {
        if (mw.minimized || !IsWindow(mw.hwnd)) continue;
        if (!any) {
            l = mw.virt.x; t = mw.virt.y; r = mw.virt.Right(); b = mw.virt.Bottom();
            any = true;
        } else {
            l = std::min(l, mw.virt.x); t = std::min(t, mw.virt.y);
            r = std::max(r, mw.virt.Right()); b = std::max(b, mw.virt.Bottom());
        }
    }
    if (any) out = VRect{ l, t, r - l, b - t };
    return any;
}

void WindowTracker::Park(HWND h) {
    auto* mw = Find(h);
    if (!mw || mw->parked || mw->excluded || mw->minimized) return;
    RECT vs = GetVirtualScreenRect();
    mw->parked = true;
    SetWindowPos(mw->hwnd, nullptr, vs.right + 160, vs.top, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    RECT r{};
    if (GetWindowRect(mw->hwnd, &r)) mw->expected = r;
}

void WindowTracker::Unpark(HWND h) {
    auto* mw = Find(h);
    if (!mw || !mw->parked) return;
    mw->parked = false;
    ApplyOne(*mw);
}
