#pragma once
// Application orchestrator: owns the camera, the frame loop and every module,
// and turns posted input events into camera/window updates.

#include "Common.h"
#include "Camera.h"
#include "Config.h"
#include "CanvasWindow.h"
#include "WindowTracker.h"
#include "Snap.h"
#include "Input.h"
#include "FpsOverlay.h"
#include "Tray.h"
#include "PreviewManager.h"

#include <deque>

class App {
public:
    bool Init(HINSTANCE inst);
    int Run();
    void Shutdown();

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    void Frame();
    void RenderCanvas();

    // input events (posted from hooks / canvas)
    void OnZoom(int delta, POINT pt);
    void OnPanBegin(POINT pt, bool maybeClick);
    void OnPanMove(POINT pt);
    void OnPanEnd(POINT pt, bool maybeClick);
    void OnDragBegin(POINT pt, bool cluster);
    void OnDragMove(POINT pt);
    void OnDragEnd();
    void OnResizeBegin(POINT pt, bool cluster);
    void OnResizeMove(POINT pt);
    void OnResizeEnd();
    void OnHotkey(int id);
    void OnPreviewFirstFrame(HWND hwnd);

    // camera actions
    void ZoomToFit();
    void GoHome();
    void CenterForeground();
    void ResetZoom();
    void GoBookmark(int slot);
    void SaveBookmark(int slot);
    void FlyTo(const Camera& target);

    void UpdatePreviewMode();
    void EnterPreviewMode();
    void ExitPreviewMode();

    static bool CameraNear(const Camera& a, const Camera& b);
    POINT ScreenCenter() const;

    HINSTANCE m_inst = nullptr;
    HWND m_hwnd = nullptr; // hidden main window (hotkeys, tray, posted events)
    HANDLE m_timer = nullptr;

    Config m_cfg;
    Camera m_cam;
    CameraAnimator m_anim;
    CanvasWindow m_canvas;
    WindowTracker m_tracker;
    InputHook m_input;
    FpsOverlay m_fps;
    TrayIcon m_tray;
    PreviewManager m_preview;

    double m_lastFrameTime = 0.0;
    bool m_cameraDirty = true;  // windows need repositioning
    bool m_renderDirty = true;  // canvas needs a redraw

    // smooth wheel zoom
    bool m_zoomAnimating = false;
    double m_zoomTarget = 1.0;
    Vec2 m_zoomAnchor{};

    // canvas panning
    bool m_panning = false;
    POINT m_panLast{};
    POINT m_panStart{};
    struct PanSample { double t; Vec2 offset; };
    std::deque<PanSample> m_panSamples;

    // window dragging / resizing (virtual space)
    bool m_dragging = false;
    bool m_resizing = false;
    bool m_resizeCluster = false;
    std::vector<HWND> m_dragSet;
    std::vector<VRect> m_dragStartRects;
    Vec2 m_dragStartMouseV{};
    VRect m_dragBounds{};
    SnapState m_snap;

    // camera toggles
    Camera m_preFit;
    Camera m_fitCam;
    bool m_fitActive = false;
    Camera m_preHome;
    bool m_preHomeSet = false;

    bool m_previewMode = false;
    HWND m_focusAfterFly = nullptr;
};
