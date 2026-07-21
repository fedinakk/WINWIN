#include "App.h"

#include <timeapi.h>

namespace {
const wchar_t kMainClass[] = L"InfiniteDeskMain";
}

bool App::Init(HINSTANCE inst) {
    m_inst = inst;
    m_cfg.Load();
    timeBeginPeriod(1);

    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.lpszClassName = kMainClass;
    RegisterClassExW(&wc);
    m_hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, kMainClass, L"InfiniteDesk",
                             WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, inst, this);
    if (!m_hwnd) return false;

    if (!m_canvas.Create(inst, m_hwnd, &m_cam, m_cfg)) return false;
    m_fps.Create(inst);

    m_tracker.Init(m_hwnd, &m_cam, m_canvas.Hwnd(), m_fps.Hwnd());
    m_tracker.AdoptExistingWindows();

    if (!m_input.Install(m_hwnd, m_canvas.Hwnd(), m_fps.Hwnd())) return false;
    m_input.RegisterHotkeys(m_hwnd, m_cfg);
    m_tray.Create(m_hwnd);

    // WGC previews are optional: without them the app still works with
    // geometric zoom only.
    if (m_cfg.previewEnabled)
        m_preview.Init(m_hwnd, m_canvas.D3DDevice(), m_canvas.D2DFactory(),
                       m_canvas.D2DContext());

    m_fps.SetVisible(m_cfg.showFps);
    m_zoomTarget = m_cam.scale;
    m_lastFrameTime = QpcNow();
    return true;
}

void App::Shutdown() {
    m_preview.Shutdown();
    m_input.UnregisterHotkeys(m_hwnd);
    m_input.Uninstall();
    m_tracker.Shutdown();
    m_tray.Destroy();
    m_fps.Destroy();
    m_canvas.Destroy();
    m_cfg.SaveBookmarks();
    if (m_timer) CloseHandle(m_timer);
    m_timer = nullptr;
    if (m_hwnd) DestroyWindow(m_hwnd);
    m_hwnd = nullptr;
    timeEndPeriod(1);
}

int App::Run() {
    m_timer = CreateWaitableTimerExW(nullptr, nullptr,
                                     CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (!m_timer)
        m_timer = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS);
    if (!m_timer) return 1;

    const double period = 1.0 / (double)m_cfg.targetFps;
    double next = QpcNow() + period;
    auto arm = [&](double delaySec) {
        LARGE_INTEGER due;
        due.QuadPart = -(LONGLONG)std::max(1.0, delaySec * 1e7);
        SetWaitableTimer(m_timer, &due, 0, nullptr, nullptr, FALSE);
    };
    arm(period);

    for (;;) {
        DWORD r = MsgWaitForMultipleObjectsEx(1, &m_timer, INFINITE, QS_ALLINPUT,
                                              MWMO_INPUTAVAILABLE);
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) return (int)msg.wParam;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (r == WAIT_OBJECT_0) {
            Frame();
            double now = QpcNow();
            next += period;
            if (next <= now) next = now + period; // we fell behind: resync
            arm(next - now);
        }
    }
}

void App::Frame() {
    const double now = QpcNow();
    const double dt = Clamp(now - m_lastFrameTime, 0.0001, 0.1);
    m_lastFrameTime = now;

    // Smooth wheel zoom towards the anchor point.
    if (m_zoomAnimating) {
        const double cur = m_cam.scale;
        const double t = 1.0 - std::exp(-18.0 * dt);
        double ns = std::exp(std::log(cur) + (std::log(m_zoomTarget) - std::log(cur)) * t);
        if (std::abs(std::log(ns / m_zoomTarget)) < 0.002) {
            ns = m_zoomTarget;
            m_zoomAnimating = false;
        }
        m_cam.ZoomAroundScreenPoint(ns, m_zoomAnchor);
        m_cameraDirty = true;
    }

    // Camera fly / pan inertia.
    if (m_anim.Tick(m_cam, now, dt, m_cfg.inertiaFriction))
        m_cameraDirty = true;

    if (m_cameraDirty) {
        m_cam.scale = Clamp(m_cam.scale, m_cfg.minScale, m_cfg.maxScale);
        m_tracker.ApplyCamera();
        UpdatePreviewMode();
        m_renderDirty = true;
        m_cameraDirty = false;
    }

    // Deferred focus after "click a preview to fly to the window".
    if (m_focusAfterFly && !m_anim.Flying()) {
        HWND h = m_focusAfterFly;
        m_focusAfterFly = nullptr;
        if (IsWindow(h)) {
            SetForegroundWindow(h);
        }
    }

    if (m_renderDirty) {
        RenderCanvas();
        m_renderDirty = false;
    }

    m_fps.ReportFrame(dt);
}

void App::RenderCanvas() {
    std::vector<PreviewDraw> previews;
    if (m_previewMode && m_preview.Available()) {
        auto lock = m_preview.LockForDraw();
        for (auto& mw : m_tracker.Windows()) {
            if (!mw.parked) continue;
            PreviewDraw p;
            p.hwnd = mw.hwnd;
            p.virt = mw.virt;
            p.bitmap = m_preview.AcquireBitmap(mw.hwnd);
            wchar_t title[256]{};
            GetWindowTextW(mw.hwnd, title, 256);
            p.title = title;
            previews.push_back(std::move(p));
        }
        m_canvas.Render(previews); // draw while holding the frame lock
        return;
    }
    m_canvas.Render(previews);
}

// ---------------------------------------------------------------- input ----

void App::OnZoom(int delta, POINT pt) {
    m_anim.CancelFly();
    m_anim.StopInertia();
    if (!m_zoomAnimating) m_zoomTarget = m_cam.scale;
    m_zoomTarget = Clamp(m_zoomTarget * std::pow(m_cfg.zoomStep, delta / 120.0),
                         m_cfg.minScale, m_cfg.maxScale);
    m_zoomAnchor = { (double)pt.x, (double)pt.y };
    m_zoomAnimating = true;
}

void App::OnPanBegin(POINT pt, bool /*maybeClick*/) {
    m_panning = true;
    m_panLast = pt;
    m_panStart = pt;
    m_panSamples.clear();
    m_anim.CancelFly();
    m_anim.StopInertia();
    m_zoomAnimating = false;
    m_panSamples.push_back({ QpcNow(), m_cam.offset });
}

void App::OnPanMove(POINT pt) {
    if (!m_panning) return;
    const double dx = (double)(pt.x - m_panLast.x);
    const double dy = (double)(pt.y - m_panLast.y);
    m_panLast = pt;
    m_cam.offset.x -= dx / m_cam.scale;
    m_cam.offset.y -= dy / m_cam.scale;
    m_cameraDirty = true;

    const double now = QpcNow();
    m_panSamples.push_back({ now, m_cam.offset });
    while (m_panSamples.size() > 2 && now - m_panSamples.front().t > 0.12)
        m_panSamples.pop_front();
}

void App::OnPanEnd(POINT pt, bool maybeClick) {
    if (!m_panning) return;
    m_panning = false;

    const int moved = abs(pt.x - m_panStart.x) + abs(pt.y - m_panStart.y);
    if (maybeClick && moved < 5) {
        // A click on empty canvas. In preview mode: fly to the clicked window.
        if (m_previewMode) {
            Vec2 v = m_cam.ScreenToVirtual({ (double)pt.x, (double)pt.y });
            for (auto& mw : m_tracker.Windows()) {
                if (!mw.parked) continue;
                if (v.x >= mw.virt.x && v.x <= mw.virt.Right() &&
                    v.y >= mw.virt.y && v.y <= mw.virt.Bottom()) {
                    POINT c = ScreenCenter();
                    Camera target;
                    target.scale = 1.0;
                    target.offset = { mw.virt.Center().x - c.x, mw.virt.Center().y - c.y };
                    m_focusAfterFly = mw.hwnd;
                    FlyTo(target);
                    break;
                }
            }
        }
        return;
    }

    // Flick -> inertia. Velocity measured on the camera offset itself
    // (virtual px/s), so behavior is zoom-consistent.
    if (m_panSamples.size() >= 2) {
        const auto& a = m_panSamples.front();
        const auto& b = m_panSamples.back();
        const double dt = b.t - a.t;
        if (dt > 0.005) {
            Vec2 vel = { (b.offset.x - a.offset.x) / dt, (b.offset.y - a.offset.y) / dt };
            if (vel.Len() * m_cam.scale > m_cfg.inertiaMinSpeed) // screen-speed gate
                m_anim.StartInertia(vel);
        }
    }
    m_panSamples.clear();
}

void App::OnDragBegin(POINT pt, bool cluster) {
    auto* mw = m_tracker.HitTest(pt);
    if (!mw || mw->excluded || mw->minimized || mw->parked) return;

    m_dragging = true;
    m_snap = SnapState{};
    m_dragSet.clear();
    m_dragStartRects.clear();

    if (cluster && m_cfg.snapEnabled) {
        m_dragSet = ClusterOf(mw->hwnd, m_tracker.Windows(), 3.0, m_cfg.snapGap);
    } else {
        m_dragSet.push_back(mw->hwnd);
    }

    bool first = true;
    for (HWND h : m_dragSet) {
        auto* w = m_tracker.Find(h);
        VRect r = w ? w->virt : VRect{};
        m_dragStartRects.push_back(r);
        if (first) { m_dragBounds = r; first = false; }
        else {
            double l = std::min(m_dragBounds.x, r.x), t = std::min(m_dragBounds.y, r.y);
            double rt = std::max(m_dragBounds.Right(), r.Right());
            double bt = std::max(m_dragBounds.Bottom(), r.Bottom());
            m_dragBounds = { l, t, rt - l, bt - t };
        }
    }
    m_dragStartMouseV = m_cam.ScreenToVirtual({ (double)pt.x, (double)pt.y });
}

void App::OnDragMove(POINT pt) {
    if (!m_dragging || m_dragSet.empty()) return;
    Vec2 v = m_cam.ScreenToVirtual({ (double)pt.x, (double)pt.y });
    Vec2 delta = v - m_dragStartMouseV;
    Vec2 raw = { m_dragBounds.x + delta.x, m_dragBounds.y + delta.y };

    Vec2 snapped = raw;
    if (m_cfg.snapEnabled) {
        const double cap = m_cfg.snapDistance / m_cam.scale;
        snapped = SnapMove(m_dragBounds, raw, m_tracker.Windows(), m_dragSet,
                           cap, cap * m_cfg.snapReleaseFactor, m_cfg.snapGap, m_snap);
    }
    Vec2 applied = snapped - Vec2{ m_dragBounds.x, m_dragBounds.y };

    for (size_t i = 0; i < m_dragSet.size(); ++i) {
        auto* w = m_tracker.Find(m_dragSet[i]);
        if (!w) continue;
        w->virt.x = m_dragStartRects[i].x + applied.x;
        w->virt.y = m_dragStartRects[i].y + applied.y;
        m_tracker.ApplyOne(*w);
    }
}

void App::OnDragEnd() {
    m_dragging = false;
    m_dragSet.clear();
    m_dragStartRects.clear();
}

void App::OnResizeBegin(POINT pt, bool cluster) {
    auto* mw = m_tracker.HitTest(pt);
    if (!mw || mw->excluded || mw->minimized || mw->parked) return;

    m_resizing = true;
    m_resizeCluster = cluster;
    m_snap = SnapState{};
    m_dragSet.clear();
    m_dragStartRects.clear();

    if (cluster && m_cfg.snapEnabled) {
        m_dragSet = ClusterOf(mw->hwnd, m_tracker.Windows(), 3.0, m_cfg.snapGap);
    } else {
        m_dragSet.push_back(mw->hwnd);
    }
    bool first = true;
    for (HWND h : m_dragSet) {
        auto* w = m_tracker.Find(h);
        VRect r = w ? w->virt : VRect{};
        m_dragStartRects.push_back(r);
        if (first) { m_dragBounds = r; first = false; }
        else {
            double l = std::min(m_dragBounds.x, r.x), t = std::min(m_dragBounds.y, r.y);
            double rt = std::max(m_dragBounds.Right(), r.Right());
            double bt = std::max(m_dragBounds.Bottom(), r.Bottom());
            m_dragBounds = { l, t, rt - l, bt - t };
        }
    }
    m_dragStartMouseV = m_cam.ScreenToVirtual({ (double)pt.x, (double)pt.y });
}

void App::OnResizeMove(POINT pt) {
    if (!m_resizing || m_dragSet.empty()) return;
    Vec2 v = m_cam.ScreenToVirtual({ (double)pt.x, (double)pt.y });
    Vec2 delta = v - m_dragStartMouseV;

    if (m_resizeCluster && m_dragSet.size() > 1) {
        // Scale the whole cluster around its top-left corner.
        const double base = std::max(120.0, m_dragBounds.w);
        const double f = Clamp(1.0 + delta.x / base, 0.2, 5.0);
        for (size_t i = 0; i < m_dragSet.size(); ++i) {
            auto* w = m_tracker.Find(m_dragSet[i]);
            if (!w) continue;
            const VRect& s = m_dragStartRects[i];
            w->virt.x = m_dragBounds.x + (s.x - m_dragBounds.x) * f;
            w->virt.y = m_dragBounds.y + (s.y - m_dragBounds.y) * f;
            w->virt.w = s.w * f;
            w->virt.h = s.h * f;
            m_tracker.ApplyOne(*w);
        }
        return;
    }

    auto* w = m_tracker.Find(m_dragSet[0]);
    if (!w) return;
    const VRect& s = m_dragStartRects[0];
    Vec2 rawBR = { s.Right() + delta.x, s.Bottom() + delta.y };
    if (m_cfg.snapEnabled) {
        const double cap = m_cfg.snapDistance / m_cam.scale;
        rawBR = SnapResize(s, rawBR, m_tracker.Windows(), m_dragSet,
                           cap, cap * m_cfg.snapReleaseFactor, m_cfg.snapGap, m_snap);
    }
    const double minSide = 60.0;
    w->virt.w = std::max(minSide, rawBR.x - s.x);
    w->virt.h = std::max(minSide, rawBR.y - s.y);
    m_tracker.ApplyOne(*w);
}

void App::OnResizeEnd() {
    m_resizing = false;
    m_dragSet.clear();
    m_dragStartRects.clear();
}

// ------------------------------------------------------------- hotkeys ----

void App::OnHotkey(int id) {
    switch (id) {
    case HK_ZOOM_FIT:   ZoomToFit(); break;
    case HK_HOME:       GoHome(); break;
    case HK_CENTER:     CenterForeground(); break;
    case HK_RESET_ZOOM: ResetZoom(); break;
    case HK_TOGGLE_FPS:
        m_cfg.showFps = !m_cfg.showFps;
        m_fps.SetVisible(m_cfg.showFps);
        break;
    case HK_EXIT:
        PostQuitMessage(0);
        break;
    default:
        if (id >= HK_BOOKMARK_1 && id <= HK_BOOKMARK_4) GoBookmark(id - HK_BOOKMARK_1);
        else if (id >= HK_SAVE_BOOKMARK_1 && id <= HK_SAVE_BOOKMARK_4)
            SaveBookmark(id - HK_SAVE_BOOKMARK_1);
        break;
    }
}

void App::FlyTo(const Camera& target) {
    m_zoomAnimating = false;
    Camera clamped = target;
    clamped.scale = Clamp(clamped.scale, m_cfg.minScale, m_cfg.maxScale);
    m_anim.FlyTo(m_cam, clamped, QpcNow(), m_cfg.flyDurationMs / 1000.0);
}

bool App::CameraNear(const Camera& a, const Camera& b) {
    return std::abs(a.offset.x - b.offset.x) < 2.0 &&
           std::abs(a.offset.y - b.offset.y) < 2.0 &&
           std::abs(std::log(a.scale / b.scale)) < 0.02;
}

POINT App::ScreenCenter() const {
    RECT vs = GetVirtualScreenRect();
    // Center of the PRIMARY monitor: that is where the "camera lens" sits.
    MONITORINFO mi{ sizeof(mi) };
    GetMonitorInfoW(MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY), &mi);
    (void)vs;
    return POINT{ (mi.rcMonitor.left + mi.rcMonitor.right) / 2,
                  (mi.rcMonitor.top + mi.rcMonitor.bottom) / 2 };
}

void App::ZoomToFit() {
    if (m_fitActive && CameraNear(m_cam, m_fitCam)) {
        m_fitActive = false;
        FlyTo(m_preFit);
        return;
    }
    VRect b{};
    if (!m_tracker.ContentBounds(b)) return;
    // 6% margin around the content.
    const double mx = b.w * 0.06 + 32.0, my = b.h * 0.06 + 32.0;
    b = { b.x - mx, b.y - my, b.w + 2 * mx, b.h + 2 * my };

    MONITORINFO mi{ sizeof(mi) };
    GetMonitorInfoW(MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY), &mi);
    const double sw = (double)(mi.rcMonitor.right - mi.rcMonitor.left);
    const double sh = (double)(mi.rcMonitor.bottom - mi.rcMonitor.top);

    Camera target;
    target.scale = Clamp(std::min(sw / b.w, sh / b.h), m_cfg.minScale, m_cfg.maxScale);
    // Center content in the primary monitor.
    const double cx = mi.rcMonitor.left + sw / 2.0, cy = mi.rcMonitor.top + sh / 2.0;
    target.offset = { b.Center().x - cx / target.scale, b.Center().y - cy / target.scale };

    m_preFit = m_cam;
    m_fitCam = target;
    m_fitActive = true;
    FlyTo(target);
}

void App::GoHome() {
    Camera home; // offset (0,0), scale 1 - the real desktop as it was
    if (CameraNear(m_cam, home)) {
        if (m_preHomeSet) FlyTo(m_preHome);
        return;
    }
    m_preHome = m_cam;
    m_preHomeSet = true;
    FlyTo(home);
}

void App::CenterForeground() {
    HWND fg = GetForegroundWindow();
    auto* mw = m_tracker.Find(fg ? GetAncestor(fg, GA_ROOT) : nullptr);
    if (!mw) return;
    POINT c = ScreenCenter();
    Camera target = m_cam;
    target.offset = { mw->virt.Center().x - c.x / m_cam.scale,
                      mw->virt.Center().y - c.y / m_cam.scale };
    FlyTo(target);
}

void App::ResetZoom() {
    POINT c = ScreenCenter();
    Camera target = m_cam;
    Vec2 v = m_cam.ScreenToVirtual({ (double)c.x, (double)c.y });
    target.scale = 1.0;
    target.offset = { v.x - c.x, v.y - c.y };
    FlyTo(target);
}

void App::GoBookmark(int slot) {
    if (slot < 0 || slot > 3 || !m_cfg.bookmarks[slot].set) return;
    Camera target;
    target.offset = { m_cfg.bookmarks[slot].x, m_cfg.bookmarks[slot].y };
    target.scale = m_cfg.bookmarks[slot].scale;
    FlyTo(target);
}

void App::SaveBookmark(int slot) {
    if (slot < 0 || slot > 3) return;
    m_cfg.bookmarks[slot] = { true, m_cam.offset.x, m_cam.offset.y, m_cam.scale };
    m_cfg.SaveBookmarks();
}

// ------------------------------------------------------------ previews ----

void App::UpdatePreviewMode() {
    if (!m_preview.Available() || !m_cfg.previewEnabled) return;
    if (!m_previewMode && m_cam.scale < m_cfg.previewBelowScale) EnterPreviewMode();
    else if (m_previewMode && m_cam.scale > m_cfg.previewAboveScale) ExitPreviewMode();
}

void App::EnterPreviewMode() {
    m_previewMode = true;
    for (auto& mw : m_tracker.Windows()) {
        if (mw.excluded || mw.minimized) continue;
        m_preview.Start(mw.hwnd);
        // Parking happens per-window in OnPreviewFirstFrame - no blink.
    }
}

void App::ExitPreviewMode() {
    m_previewMode = false;
    for (auto& mw : m_tracker.Windows())
        if (mw.parked) m_tracker.Unpark(mw.hwnd);
    m_preview.StopAll();
    m_renderDirty = true;
}

void App::OnPreviewFirstFrame(HWND hwnd) {
    if (!m_previewMode) return; // stale: user already zoomed back in
    if (m_tracker.Find(hwnd)) m_tracker.Park(hwnd);
    m_renderDirty = true;
}

// ------------------------------------------------------------- wndproc ----

LRESULT CALLBACK App::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* cs = (CREATESTRUCTW*)lp;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    auto* self = (App*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (!self) return DefWindowProcW(hwnd, msg, wp, lp);
    return self->HandleMessage(hwnd, msg, wp, lp);
}

LRESULT App::HandleMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case IDM_WM_ZOOM:
        OnZoom((int)(INT_PTR)wp, LParamToPoint(lp));
        return 0;
    case IDM_WM_PAN_BEGIN:
        OnPanBegin(LParamToPoint(lp), wp == 1);
        return 0;
    case IDM_WM_PAN_MOVE:
        OnPanMove(LParamToPoint(lp));
        return 0;
    case IDM_WM_PAN_END:
        OnPanEnd(LParamToPoint(lp), wp == 1);
        return 0;
    case IDM_WM_DRAG_BEGIN:
        OnDragBegin(LParamToPoint(lp), wp == 1);
        return 0;
    case IDM_WM_DRAG_MOVE:
        OnDragMove(LParamToPoint(lp));
        return 0;
    case IDM_WM_DRAG_END:
        OnDragEnd();
        return 0;
    case IDM_WM_RESIZE_BEGIN:
        OnResizeBegin(LParamToPoint(lp), wp == 1);
        return 0;
    case IDM_WM_RESIZE_MOVE:
        OnResizeMove(LParamToPoint(lp));
        return 0;
    case IDM_WM_RESIZE_END:
        OnResizeEnd();
        return 0;

    case IDM_WM_WINEVENT: {
        HWND target = (HWND)lp;
        m_tracker.OnWinEvent((DWORD)wp, target);
        if ((DWORD)wp == EVENT_OBJECT_DESTROY || (DWORD)wp == EVENT_OBJECT_HIDE) {
            if (m_preview.Capturing(target)) {
                m_preview.Stop(target);
                m_renderDirty = true;
            }
        } else if (m_previewMode && (DWORD)wp == EVENT_OBJECT_SHOW) {
            // A window opened while zoomed out: capture it too.
            if (m_tracker.Find(target)) m_preview.Start(target);
        }
        return 0;
    }

    case IDM_WM_PREVIEW_FRAME:
        OnPreviewFirstFrame((HWND)lp);
        return 0;

    case IDM_WM_TRAY:
        m_tray.OnCallback(wp, lp, m_cfg.showFps);
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_TRAY_EXIT: PostQuitMessage(0); break;
        case IDC_TRAY_TOGGLE_FPS:
            m_cfg.showFps = !m_cfg.showFps;
            m_fps.SetVisible(m_cfg.showFps);
            break;
        case IDC_TRAY_ZOOM_FIT: ZoomToFit(); break;
        case IDC_TRAY_RESET: {
            Camera home;
            FlyTo(home);
            break;
        }
        case IDC_TRAY_GATHER: {
            // Pull every window into the current viewport (rescue action).
            RECT vs = GetVirtualScreenRect();
            VRect view = m_cam.ScreenToVirtual(vs);
            int i = 0;
            for (auto& mw : m_tracker.Windows()) {
                if (mw.excluded || mw.minimized) continue;
                mw.virt.x = view.x + 40 + (i % 6) * 60;
                mw.virt.y = view.y + 40 + (i % 6) * 50;
                ++i;
                m_tracker.ApplyOne(mw);
            }
            m_renderDirty = true;
            break;
        }
        }
        return 0;

    case WM_HOTKEY:
        OnHotkey((int)wp);
        return 0;

    case WM_DISPLAYCHANGE:
        m_canvas.OnDisplayChange();
        m_renderDirty = true;
        return 0;

    case WM_CLOSE:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
