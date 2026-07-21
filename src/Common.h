#pragma once
// Common includes and small helpers shared by all InfiniteDesk modules.

#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

using Microsoft::WRL::ComPtr;

// Custom messages posted from hooks/event callbacks to the main window.
// Heavy work never happens inside hook callbacks - only here, on the main thread.
enum : UINT {
    IDM_WM_ZOOM        = WM_APP + 1,  // wParam: wheel delta (signed), lParam: screen pt
    IDM_WM_PAN_BEGIN   = WM_APP + 2,  // lParam: screen pt
    IDM_WM_PAN_MOVE    = WM_APP + 3,  // lParam: screen pt
    IDM_WM_PAN_END     = WM_APP + 4,  // lParam: screen pt
    IDM_WM_DRAG_BEGIN  = WM_APP + 5,  // wParam: 1 = cluster (Shift), lParam: screen pt
    IDM_WM_DRAG_MOVE   = WM_APP + 6,  // lParam: screen pt
    IDM_WM_DRAG_END    = WM_APP + 7,  // lParam: screen pt
    IDM_WM_RESIZE_BEGIN= WM_APP + 8,  // wParam: 1 = cluster, lParam: screen pt
    IDM_WM_RESIZE_MOVE = WM_APP + 9,  // lParam: screen pt
    IDM_WM_RESIZE_END  = WM_APP + 10, // lParam: screen pt
    IDM_WM_WINEVENT    = WM_APP + 11, // wParam: event id, lParam: HWND
    IDM_WM_TRAY        = WM_APP + 12, // tray icon callback
    IDM_WM_PREVIEW_FRAME = WM_APP + 13, // a WGC frame arrived (repaint hint)
};

// Tray menu command ids.
enum : UINT {
    IDC_TRAY_EXIT = 100,
    IDC_TRAY_TOGGLE_FPS,
    IDC_TRAY_ZOOM_FIT,
    IDC_TRAY_RESET,
    IDC_TRAY_GATHER,
};

struct Vec2 {
    double x = 0.0, y = 0.0;
    Vec2 operator+(const Vec2& o) const { return {x + o.x, y + o.y}; }
    Vec2 operator-(const Vec2& o) const { return {x - o.x, y - o.y}; }
    Vec2 operator*(double k) const { return {x * k, y * k}; }
    double Len() const { return std::sqrt(x * x + y * y); }
};

// Axis-aligned rect in virtual (canvas) coordinates.
struct VRect {
    double x = 0.0, y = 0.0, w = 0.0, h = 0.0;
    double Right() const { return x + w; }
    double Bottom() const { return y + h; }
    Vec2 Center() const { return {x + w / 2.0, y + h / 2.0}; }
};

inline POINT LParamToPoint(LPARAM lp) {
    return POINT{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
}
inline LPARAM PointToLParam(POINT pt) {
    return MAKELPARAM((short)pt.x, (short)pt.y);
}

inline double Clamp(double v, double lo, double hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

inline double QpcNow() {
    static double freqInv = [] {
        LARGE_INTEGER f; QueryPerformanceFrequency(&f);
        return 1.0 / (double)f.QuadPart;
    }();
    LARGE_INTEGER c; QueryPerformanceCounter(&c);
    return (double)c.QuadPart * freqInv;
}

// Virtual screen bounds (all monitors), physical px.
inline RECT GetVirtualScreenRect() {
    return RECT{
        GetSystemMetrics(SM_XVIRTUALSCREEN),
        GetSystemMetrics(SM_YVIRTUALSCREEN),
        GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN),
        GetSystemMetrics(SM_YVIRTUALSCREEN) + GetSystemMetrics(SM_CYVIRTUALSCREEN),
    };
}
