#pragma once
// Semi-transparent FPS/frame-time corner overlay. A tiny topmost layered
// window (LWA_ALPHA), click-through and non-activating, redrawn ~4x/sec.

#include "Common.h"

class FpsOverlay {
public:
    bool Create(HINSTANCE inst);
    void Destroy();
    HWND Hwnd() const { return m_hwnd; }
    void SetVisible(bool on);
    bool Visible() const { return m_visible; }
    // Called every frame from the main loop; repaints on its own schedule.
    void ReportFrame(double frameSeconds);

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    void Paint(HDC dc, RECT rc);
    void Reposition();

    HWND m_hwnd = nullptr;
    bool m_visible = false;
    double m_accum = 0.0;
    int m_frames = 0;
    double m_lastText = 0.0;
    double m_fps = 0.0;
    double m_ms = 0.0;
    HFONT m_font = nullptr;
};
