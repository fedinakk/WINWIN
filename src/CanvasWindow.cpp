#include "CanvasWindow.h"

#include <d2d1helper.h>
#include <d2d1_1helper.h>
#include <wincodec.h>

namespace {

const wchar_t kClassName[] = L"InfiniteDeskCanvas";

D2D1_COLOR_F ColorFromArgb(uint32_t c) {
    return D2D1::ColorF(
        ((c >> 16) & 0xFF) / 255.0f,
        ((c >> 8) & 0xFF) / 255.0f,
        (c & 0xFF) / 255.0f,
        ((c >> 24) & 0xFF) / 255.0f);
}

constexpr int kTilePx = 64; // grid tile bitmap size in pixels

} // namespace

bool CanvasWindow::Create(HINSTANCE inst, HWND mainWnd, const Camera* cam, const Config& cfg) {
    m_mainWnd = mainWnd;
    m_cam = cam;
    m_bgColor = ColorFromArgb(cfg.backgroundColor);
    m_gridStep = cfg.gridStep;
    m_drawWallpaper = cfg.drawWallpaper;

    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.lpszClassName = kClassName;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    RegisterClassExW(&wc);

    m_bounds = GetVirtualScreenRect();
    m_hwnd = CreateWindowExW(
        WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_NOREDIRECTIONBITMAP,
        kClassName, L"InfiniteDesk Canvas", WS_POPUP,
        m_bounds.left, m_bounds.top,
        m_bounds.right - m_bounds.left, m_bounds.bottom - m_bounds.top,
        nullptr, nullptr, inst, this);
    if (!m_hwnd) return false;

    if (!InitGraphics()) return false;

    // The grid tile color comes from config.
    m_gridDotArgb = cfg.gridDotColor;
    CreateGridTile();
    if (m_drawWallpaper) LoadWallpaper();

    MONITORINFO mi{ sizeof(mi) };
    GetMonitorInfoW(MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY), &mi);
    m_wallpaperRect = mi.rcMonitor;

    ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
    SetWindowPos(m_hwnd, HWND_BOTTOM, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    return true;
}

void CanvasWindow::Destroy() {
    m_target.Reset();
    m_gridBrush.Reset();
    m_gridTile.Reset();
    m_wallpaper.Reset();
    m_solid.Reset();
    m_textFormat.Reset();
    m_dwrite.Reset();
    m_dcompVisual.Reset();
    m_dcompTarget.Reset();
    m_dcompDevice.Reset();
    m_dc.Reset();
    m_d2dDevice.Reset();
    m_d2dFactory.Reset();
    m_swap.Reset();
    m_d3d.Reset();
    if (m_hwnd) DestroyWindow(m_hwnd);
    m_hwnd = nullptr;
}

LRESULT CALLBACK CanvasWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* self = (CanvasWindow*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (msg == WM_NCCREATE) {
        auto* cs = (CREATESTRUCTW*)lp;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    if (!self) return DefWindowProcW(hwnd, msg, wp, lp);

    switch (msg) {
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;

    case WM_WINDOWPOSCHANGING: {
        // Pin the canvas to the bottom of the z-order, always.
        auto* pos = (WINDOWPOS*)lp;
        pos->hwndInsertAfter = HWND_BOTTOM;
        pos->flags &= ~SWP_NOZORDER;
        return 0;
    }

    case WM_MOUSEWHEEL: {
        // Cursor is over empty canvas: zoom without any modifier.
        POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) }; // already screen coords
        PostMessageW(self->m_mainWnd, IDM_WM_ZOOM,
                     (WPARAM)(INT_PTR)GET_WHEEL_DELTA_WPARAM(wp), PointToLParam(pt));
        return 0;
    }

    case WM_MBUTTONDOWN:
    case WM_LBUTTONDOWN: {
        POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        ClientToScreen(hwnd, &pt);
        SetCapture(hwnd);
        self->m_dragButton = (msg == WM_LBUTTONDOWN) ? VK_LBUTTON : VK_MBUTTON;
        // wParam 1 marks "might be a click on a preview" (left button only).
        PostMessageW(self->m_mainWnd, IDM_WM_PAN_BEGIN,
                     msg == WM_LBUTTONDOWN ? 1 : 0, PointToLParam(pt));
        return 0;
    }

    case WM_MOUSEMOVE:
        if (self->m_dragButton) {
            POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            ClientToScreen(hwnd, &pt);
            PostMessageW(self->m_mainWnd, IDM_WM_PAN_MOVE, 0, PointToLParam(pt));
        }
        return 0;

    case WM_MBUTTONUP:
    case WM_LBUTTONUP:
        if (self->m_dragButton) {
            POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            ClientToScreen(hwnd, &pt);
            ReleaseCapture();
            PostMessageW(self->m_mainWnd, IDM_WM_PAN_END,
                         self->m_dragButton == VK_LBUTTON ? 1 : 0, PointToLParam(pt));
            self->m_dragButton = 0;
        }
        return 0;

    case WM_CAPTURECHANGED:
        self->m_dragButton = 0;
        return 0;

    case WM_ERASEBKGND:
        return 1; // DirectComposition owns all pixels

    case WM_CLOSE:
        return 0; // only the app closes the canvas
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

bool CanvasWindow::InitGraphics() {
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                   D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                                   D3D11_SDK_VERSION, m_d3d.GetAddressOf(), nullptr, nullptr);
    if (FAILED(hr)) {
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
                               D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                               D3D11_SDK_VERSION, m_d3d.GetAddressOf(), nullptr, nullptr);
        if (FAILED(hr)) return false;
    }

    ComPtr<IDXGIDevice1> dxgiDevice;
    if (FAILED(m_d3d.As(&dxgiDevice))) return false;
    dxgiDevice->SetMaximumFrameLatency(1);

    // Multithreaded factory: WGC frame copies happen on capture worker threads
    // under the D2D lock (see PreviewManager).
    D2D1_FACTORY_OPTIONS fo{};
    hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED, __uuidof(ID2D1Factory1),
                           &fo, (void**)m_d2dFactory.GetAddressOf());
    if (FAILED(hr)) return false;
    if (FAILED(m_d2dFactory->CreateDevice(dxgiDevice.Get(), m_d2dDevice.GetAddressOf()))) return false;
    if (FAILED(m_d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE,
                                                m_dc.GetAddressOf()))) return false;

    if (!CreateSizeDependent()) return false;

    if (FAILED(DCompositionCreateDevice(dxgiDevice.Get(), IID_PPV_ARGS(m_dcompDevice.GetAddressOf()))))
        return false;
    if (FAILED(m_dcompDevice->CreateTargetForHwnd(m_hwnd, TRUE, m_dcompTarget.GetAddressOf())))
        return false;
    if (FAILED(m_dcompDevice->CreateVisual(m_dcompVisual.GetAddressOf()))) return false;
    m_dcompVisual->SetContent(m_swap.Get());
    m_dcompTarget->SetRoot(m_dcompVisual.Get());
    m_dcompDevice->Commit();

    m_dc->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), m_solid.GetAddressOf());

    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                        (IUnknown**)m_dwrite.GetAddressOf());
    if (m_dwrite) {
        m_dwrite->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                                   DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                   13.0f, L"", m_textFormat.GetAddressOf());
        if (m_textFormat) {
            m_textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            m_textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            m_textFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        }
    }
    return true;
}

void CanvasWindow::ReleaseSizeDependent() {
    if (m_dc) m_dc->SetTarget(nullptr);
    m_target.Reset();
}

bool CanvasWindow::CreateSizeDependent() {
    const UINT w = (UINT)std::max(1L, m_bounds.right - m_bounds.left);
    const UINT h = (UINT)std::max(1L, m_bounds.bottom - m_bounds.top);

    if (!m_swap) {
        ComPtr<IDXGIDevice> dxgiDevice;
        m_d3d.As(&dxgiDevice);
        ComPtr<IDXGIAdapter> adapter;
        dxgiDevice->GetAdapter(adapter.GetAddressOf());
        ComPtr<IDXGIFactory2> factory;
        adapter->GetParent(IID_PPV_ARGS(factory.GetAddressOf()));

        DXGI_SWAP_CHAIN_DESC1 desc{};
        desc.Width = w;
        desc.Height = h;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc = { 1, 0 };
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 2;
        desc.Scaling = DXGI_SCALING_STRETCH;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
        if (FAILED(factory->CreateSwapChainForComposition(m_d3d.Get(), &desc, nullptr,
                                                          m_swap.GetAddressOf())))
            return false;
    } else {
        ReleaseSizeDependent();
        if (FAILED(m_swap->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0))) return false;
    }

    ComPtr<IDXGISurface> surface;
    if (FAILED(m_swap->GetBuffer(0, IID_PPV_ARGS(surface.GetAddressOf())))) return false;
    D2D1_BITMAP_PROPERTIES1 bp = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    if (FAILED(m_dc->CreateBitmapFromDxgiSurface(surface.Get(), &bp, m_target.GetAddressOf())))
        return false;
    m_dc->SetTarget(m_target.Get());
    return true;
}

void CanvasWindow::CreateGridTile() {
    // A single dot tile; WRAP extend mode turns it into an infinite grid on
    // the GPU, so the grid costs one FillRectangle no matter the zoom.
    std::vector<uint32_t> px((size_t)kTilePx * kTilePx, 0u);
    const float cx = kTilePx / 2.0f, cy = kTilePx / 2.0f;
    const float radius = 2.6f;
    const uint32_t c = m_gridDotArgb;
    const float a0 = ((c >> 24) & 0xFF) / 255.0f;
    const float r0 = ((c >> 16) & 0xFF) / 255.0f;
    const float g0 = ((c >> 8) & 0xFF) / 255.0f;
    const float b0 = (c & 0xFF) / 255.0f;
    for (int y = 0; y < kTilePx; ++y) {
        for (int x = 0; x < kTilePx; ++x) {
            float d = std::sqrt((x + 0.5f - cx) * (x + 0.5f - cx) +
                                (y + 0.5f - cy) * (y + 0.5f - cy));
            float cov = (float)Clamp(radius + 0.8f - d, 0.0, 1.0); // soft edge
            if (cov <= 0.0f) continue;
            float a = a0 * cov;
            uint32_t A = (uint32_t)(a * 255.0f + 0.5f);
            uint32_t R = (uint32_t)(r0 * a * 255.0f + 0.5f); // premultiplied
            uint32_t G = (uint32_t)(g0 * a * 255.0f + 0.5f);
            uint32_t B = (uint32_t)(b0 * a * 255.0f + 0.5f);
            px[(size_t)y * kTilePx + x] = (A << 24) | (R << 16) | (G << 8) | B;
        }
    }
    D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_NONE,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    if (FAILED(m_dc->CreateBitmap(D2D1::SizeU(kTilePx, kTilePx), px.data(),
                                  kTilePx * 4, &props, m_gridTile.GetAddressOf())))
        return;
    if (FAILED(m_dc->CreateBitmapBrush(m_gridTile.Get(), m_gridBrush.GetAddressOf())))
        return;
    m_gridBrush->SetExtendModeX(D2D1_EXTEND_MODE_WRAP);
    m_gridBrush->SetExtendModeY(D2D1_EXTEND_MODE_WRAP);
    m_gridBrush->SetInterpolationMode(D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
}

void CanvasWindow::LoadWallpaper() {
    wchar_t path[MAX_PATH]{};
    if (!SystemParametersInfoW(SPI_GETDESKWALLPAPER, MAX_PATH, path, 0) || !path[0])
        return;

    ComPtr<IWICImagingFactory> wic;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(wic.GetAddressOf()))))
        return;
    ComPtr<IWICBitmapDecoder> dec;
    if (FAILED(wic->CreateDecoderFromFilename(path, nullptr, GENERIC_READ,
                                              WICDecodeMetadataCacheOnLoad, dec.GetAddressOf())))
        return;
    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(dec->GetFrame(0, frame.GetAddressOf()))) return;
    ComPtr<IWICFormatConverter> conv;
    if (FAILED(wic->CreateFormatConverter(conv.GetAddressOf()))) return;
    if (FAILED(conv->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA,
                                WICBitmapDitherTypeNone, nullptr, 0.0,
                                WICBitmapPaletteTypeMedianCut)))
        return;
    m_dc->CreateBitmapFromWicBitmap(conv.Get(), nullptr, m_wallpaper.GetAddressOf());
}

void CanvasWindow::Render(const std::vector<PreviewDraw>& previews) {
    if (!m_dc || !m_target || !m_cam) return;

    const double s = m_cam->scale;
    const D2D1_MATRIX_3X2_F world = D2D1::Matrix3x2F(
        (float)s, 0.0f, 0.0f, (float)s,
        (float)(-m_cam->offset.x * s - m_bounds.left),
        (float)(-m_cam->offset.y * s - m_bounds.top));

    m_dc->BeginDraw();
    m_dc->Clear(m_bgColor);
    m_dc->SetTransform(world);

    // Visible part of the virtual plane.
    VRect vis = m_cam->ScreenToVirtual(m_bounds);
    D2D1_RECT_F visF = D2D1::RectF((float)vis.x, (float)vis.y,
                                   (float)vis.Right(), (float)vis.Bottom());

    // Wallpaper anchored at the home viewport (primary monitor at scale 1).
    if (m_wallpaper && m_drawWallpaper) {
        D2D1_RECT_F dst = D2D1::RectF(
            (float)m_wallpaperRect.left, (float)m_wallpaperRect.top,
            (float)m_wallpaperRect.right, (float)m_wallpaperRect.bottom);
        m_dc->DrawBitmap(m_wallpaper.Get(), &dst, 0.9f,
                         D2D1_INTERPOLATION_MODE_LINEAR, nullptr);
    }

    // Infinite dot grid: adapt the step so dots keep a sane screen density.
    if (m_gridBrush) {
        double step = m_gridStep;
        while (step * s < 48.0 && step < 65536.0) step *= 2.0;
        while (step * s >= 96.0 && step > 8.0) step /= 2.0;
        const float k = (float)(step / kTilePx);
        m_gridBrush->SetTransform(D2D1::Matrix3x2F::Scale(k, k));
        m_dc->FillRectangle(visF, m_gridBrush.Get());
    }

    // Live previews (windows parked offscreen while zoomed out).
    for (const auto& p : previews) {
        D2D1_RECT_F r = D2D1::RectF((float)p.virt.x, (float)p.virt.y,
                                    (float)p.virt.Right(), (float)p.virt.Bottom());
        const float bw = (float)(2.0 / s); // ~2 screen px border
        if (m_solid) {
            m_solid->SetColor(D2D1::ColorF(0.08f, 0.09f, 0.12f, 0.9f));
            m_dc->FillRectangle(r, m_solid.Get());
        }
        if (p.bitmap) {
            m_dc->DrawBitmap(p.bitmap, &r, 1.0f, D2D1_INTERPOLATION_MODE_LINEAR, nullptr);
        }
        if (m_solid) {
            m_solid->SetColor(D2D1::ColorF(0.45f, 0.55f, 0.85f, 0.85f));
            m_dc->DrawRectangle(r, m_solid.Get(), bw);
        }
    }

    // Titles in screen space (constant size regardless of zoom).
    if (!previews.empty() && m_textFormat && m_solid) {
        m_dc->SetTransform(D2D1::Matrix3x2F::Identity());
        for (const auto& p : previews) {
            if (p.title.empty()) continue;
            Vec2 tl = m_cam->VirtualToScreen(Vec2{ p.virt.x, p.virt.y });
            Vec2 br = m_cam->VirtualToScreen(Vec2{ p.virt.Right(), p.virt.Bottom() });
            float x = (float)(tl.x - m_bounds.left);
            float y = (float)(tl.y - m_bounds.top);
            float wpx = (float)(br.x - tl.x);
            if (wpx < 32.0f) continue;
            D2D1_RECT_F tr = D2D1::RectF(x + 2.0f, y - 22.0f, x + wpx - 2.0f, y - 2.0f);
            m_solid->SetColor(D2D1::ColorF(0.85f, 0.88f, 0.95f, 0.95f));
            m_dc->DrawTextW(p.title.c_str(), (UINT32)p.title.size(), m_textFormat.Get(),
                            &tr, m_solid.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }
    }

    HRESULT hr = m_dc->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        // Device lost: rebuild the whole stack next frame.
        ReleaseSizeDependent();
        m_swap.Reset();
        m_dcompVisual.Reset();
        m_dcompTarget.Reset();
        m_dcompDevice.Reset();
        m_dc.Reset();
        m_d2dDevice.Reset();
        m_d2dFactory.Reset();
        m_d3d.Reset();
        InitGraphics();
        CreateGridTile();
        if (m_drawWallpaper) LoadWallpaper();
        return;
    }
    if (m_swap) m_swap->Present(0, 0);
}

void CanvasWindow::OnDisplayChange() {
    m_bounds = GetVirtualScreenRect();
    SetWindowPos(m_hwnd, HWND_BOTTOM, m_bounds.left, m_bounds.top,
                 m_bounds.right - m_bounds.left, m_bounds.bottom - m_bounds.top,
                 SWP_NOACTIVATE);
    MONITORINFO mi{ sizeof(mi) };
    GetMonitorInfoW(MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY), &mi);
    m_wallpaperRect = mi.rcMonitor;
    CreateSizeDependent();
}
