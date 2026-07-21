#pragma once
// The canvas layer: a fullscreen DirectComposition window sitting at the
// BOTTOM of the z-order (above the wallpaper, below every user window).
// It renders the "infinite space": background color, the user's wallpaper
// anchored at the home viewport, an infinite dot grid - all through the
// shared Camera - plus live window previews when zoomed out.
//
// It never activates (WS_EX_NOACTIVATE) and never rises above user windows
// (WM_WINDOWPOSCHANGING pins it to the bottom). Input that lands on it means
// the user clicked empty canvas: wheel = zoom, middle/left drag = pan,
// left click on a preview = fly to that window.

#include "Common.h"
#include "Camera.h"
#include "Config.h"

#include <d3d11.h>
#include <dxgi1_3.h>
#include <d2d1_1.h>
#include <dcomp.h>
#include <dwrite.h>

struct PreviewDraw {
    HWND hwnd = nullptr;
    VRect virt{};             // where the window lives on the canvas
    ID2D1Bitmap1* bitmap = nullptr; // latest captured frame (may be null)
    std::wstring title;
};

class CanvasWindow {
public:
    bool Create(HINSTANCE inst, HWND mainWnd, const Camera* cam, const Config& cfg);
    void Destroy();

    HWND Hwnd() const { return m_hwnd; }
    ID3D11Device* D3DDevice() const { return m_d3d.Get(); }
    ID2D1DeviceContext* D2DContext() const { return m_dc.Get(); }
    ID2D1Factory1* D2DFactory() const { return m_d2dFactory.Get(); }

    // Renders one frame. 'previews' may be empty (normal close-up mode).
    void Render(const std::vector<PreviewDraw>& previews);

    void OnDisplayChange(); // resize swapchain to the new virtual screen

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    bool InitGraphics();
    void ReleaseSizeDependent();
    bool CreateSizeDependent();
    void CreateGridTile();
    void LoadWallpaper();

    HWND m_hwnd = nullptr;
    HWND m_mainWnd = nullptr;
    const Camera* m_cam = nullptr;
    RECT m_bounds{}; // current virtual-screen bounds covered by the window

    ComPtr<ID3D11Device> m_d3d;
    ComPtr<IDXGISwapChain1> m_swap;
    ComPtr<ID2D1Factory1> m_d2dFactory;
    ComPtr<ID2D1Device> m_d2dDevice;
    ComPtr<ID2D1DeviceContext> m_dc;
    ComPtr<ID2D1Bitmap1> m_target;
    ComPtr<IDCompositionDevice> m_dcompDevice;
    ComPtr<IDCompositionTarget> m_dcompTarget;
    ComPtr<IDCompositionVisual> m_dcompVisual;

    ComPtr<ID2D1Bitmap1> m_gridTile;
    ComPtr<ID2D1BitmapBrush1> m_gridBrush;
    ComPtr<ID2D1Bitmap1> m_wallpaper;
    ComPtr<ID2D1SolidColorBrush> m_solid;
    ComPtr<IDWriteFactory> m_dwrite;
    ComPtr<IDWriteTextFormat> m_textFormat;

    D2D1_COLOR_F m_bgColor{};
    uint32_t m_gridDotArgb = 0xFF2F3347;
    double m_gridStep = 96.0;
    bool m_drawWallpaper = true;
    RECT m_wallpaperRect{}; // primary monitor rect = home viewport
    int m_dragButton = 0;   // VK_LBUTTON/VK_MBUTTON while panning from canvas
};
