#include "FpsOverlay.h"

namespace {
const wchar_t kClassName[] = L"InfiniteDeskFps";
constexpr int kW = 172, kH = 46;
}

bool FpsOverlay::Create(HINSTANCE inst) {
    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.lpszClassName = kClassName;
    wc.hbrBackground = CreateSolidBrush(RGB(16, 17, 24));
    RegisterClassExW(&wc);

    m_hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
        kClassName, L"", WS_POPUP, 0, 0, kW, kH, nullptr, nullptr, inst, this);
    if (!m_hwnd) return false;
    SetWindowLongPtrW(m_hwnd, GWLP_USERDATA, (LONG_PTR)this);
    SetLayeredWindowAttributes(m_hwnd, 0, 205, LWA_ALPHA);

    m_font = CreateFontW(-15, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                         DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                         CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Consolas");
    Reposition();
    return true;
}

void FpsOverlay::Destroy() {
    if (m_hwnd) DestroyWindow(m_hwnd);
    m_hwnd = nullptr;
    if (m_font) DeleteObject(m_font);
    m_font = nullptr;
}

void FpsOverlay::Reposition() {
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    SetWindowPos(m_hwnd, HWND_TOPMOST, work.right - kW - 12, work.top + 12, kW, kH,
                 SWP_NOACTIVATE);
}

void FpsOverlay::SetVisible(bool on) {
    m_visible = on;
    if (!m_hwnd) return;
    if (on) {
        Reposition();
        ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
    } else {
        ShowWindow(m_hwnd, SW_HIDE);
    }
}

void FpsOverlay::ReportFrame(double frameSeconds) {
    m_accum += frameSeconds;
    m_frames++;
    m_lastText += frameSeconds;
    if (m_lastText >= 0.25 && m_frames > 0) {
        m_ms = m_accum / m_frames * 1000.0;
        m_fps = m_frames / m_accum;
        m_accum = 0.0;
        m_frames = 0;
        m_lastText = 0.0;
        if (m_visible && m_hwnd) InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}

void FpsOverlay::Paint(HDC dc, RECT rc) {
    HBRUSH bg = CreateSolidBrush(RGB(16, 17, 24));
    FillRect(dc, &rc, bg);
    DeleteObject(bg);

    SetBkMode(dc, TRANSPARENT);
    HGDIOBJ old = m_font ? SelectObject(dc, m_font) : nullptr;

    wchar_t line1[64], line2[64];
    swprintf_s(line1, L"FPS  %6.1f", m_fps);
    swprintf_s(line2, L"time %6.2f ms", m_ms);

    SetTextColor(dc, RGB(122, 224, 152));
    RECT r1{ rc.left + 12, rc.top + 5, rc.right - 8, rc.top + 24 };
    DrawTextW(dc, line1, -1, &r1, DT_LEFT | DT_SINGLELINE);
    SetTextColor(dc, RGB(170, 176, 200));
    RECT r2{ rc.left + 12, rc.top + 23, rc.right - 8, rc.bottom - 3 };
    DrawTextW(dc, line2, -1, &r2, DT_LEFT | DT_SINGLELINE);

    if (old) SelectObject(dc, old);
}

LRESULT CALLBACK FpsOverlay::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* self = (FpsOverlay*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        if (self) self->Paint(dc, rc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_DISPLAYCHANGE:
        if (self) self->Reposition();
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
