#include "PreviewManager.h"

#include <d3d11.h>
#include <d2d1_1.h>
#include <dxgi.h>
#include <inspectable.h>

#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>

namespace wgc = winrt::Windows::Graphics::Capture;
namespace wgd = winrt::Windows::Graphics::DirectX;

// One captured window: WinRT session + latest-frame texture + D2D wrapper.
class PreviewEntry {
public:
    HWND hwnd = nullptr;
    wgc::GraphicsCaptureItem item{ nullptr };
    wgc::Direct3D11CaptureFramePool pool{ nullptr };
    wgc::GraphicsCaptureSession session{ nullptr };
    winrt::event_token frameToken{};
    winrt::event_token closedToken{};
    ComPtr<ID3D11Texture2D> latest;   // updated by the capture worker
    ComPtr<ID2D1Bitmap1> bitmap;      // wraps 'latest'; render thread only
    UINT texW = 0, texH = 0;
    bool announced = false;           // first-frame message already posted

    ~PreviewEntry() { Close(); }

    void Close() {
        if (session) { session.Close(); session = nullptr; }
        if (pool) {
            if (frameToken.value) pool.FrameArrived(frameToken);
            pool.Close();
            pool = nullptr;
        }
        if (item) {
            if (closedToken.value) item.Closed(closedToken);
            item = nullptr;
        }
        frameToken = {};
        closedToken = {};
    }
};

namespace {

winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice
CreateWinrtDevice(ID3D11Device* d3d) {
    ComPtr<IDXGIDevice> dxgi;
    if (FAILED(d3d->QueryInterface(IID_PPV_ARGS(dxgi.GetAddressOf())))) return nullptr;
    winrt::com_ptr<::IInspectable> insp;
    if (FAILED(CreateDirect3D11DeviceFromDXGIDevice(dxgi.Get(), insp.put()))) return nullptr;
    return insp.as<winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice>();
}

winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice g_winrtDevice{ nullptr };

} // namespace

bool PreviewManager::Init(HWND mainWnd, ID3D11Device* d3d, ID2D1Factory1* factory,
                          ID2D1DeviceContext* dc) {
    m_mainWnd = mainWnd;
    m_d3d = d3d;
    m_factory = factory;
    m_dc = dc;
    m_available = false;
    if (!d3d || !dc || !factory) return false;

    // WGC availability check; guarded so a stripped-down system just degrades.
    try {
        if (!wgc::GraphicsCaptureSession::IsSupported()) return false;
        g_winrtDevice = CreateWinrtDevice(d3d);
        if (!g_winrtDevice) return false;
    } catch (...) {
        return false;
    }
    m_available = true;
    return true;
}

void PreviewManager::Shutdown() {
    StopAll();
    g_winrtDevice = nullptr;
    m_available = false;
}

bool PreviewManager::Capturing(HWND target) const {
    for (auto& e : m_entries)
        if (e->hwnd == target) return true;
    return false;
}

bool PreviewManager::Start(HWND target) {
    if (!m_available || Capturing(target)) return m_available;

    try {
        auto interop = winrt::get_activation_factory<wgc::GraphicsCaptureItem>()
                           .as<IGraphicsCaptureItemInterop>();
        wgc::GraphicsCaptureItem item{ nullptr };
        HRESULT hr = interop->CreateForWindow(
            target, winrt::guid_of<wgc::GraphicsCaptureItem>(), winrt::put_abi(item));
        if (FAILED(hr) || !item) return false; // some windows refuse capture

        auto entry = std::make_unique<PreviewEntry>();
        entry->hwnd = target;
        entry->item = item;
        entry->pool = wgc::Direct3D11CaptureFramePool::CreateFreeThreaded(
            g_winrtDevice, wgd::DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, item.Size());
        entry->session = entry->pool.CreateCaptureSession(item);
        try {
            entry->session.IsCursorCaptureEnabled(false);
        } catch (...) { /* older builds: cursor stays in, harmless */ }

        PreviewEntry* raw = entry.get();
        HWND mainWnd = m_mainWnd;

        entry->closedToken = item.Closed([mainWnd, target](auto&&, auto&&) {
            // Window died mid-capture; let the main thread clean up.
            PostMessageW(mainWnd, IDM_WM_WINEVENT, EVENT_OBJECT_DESTROY, (LPARAM)target);
        });

        entry->frameToken = entry->pool.FrameArrived(
            [this, raw, mainWnd, target](wgc::Direct3D11CaptureFramePool const& sender, auto&&) {
                // Capture worker thread: copy the frame, nothing else.
                try {
                    auto frame = sender.TryGetNextFrame();
                    if (!frame) return;
                    auto access = frame.Surface().as<
                        ::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
                    ComPtr<ID3D11Texture2D> tex;
                    if (FAILED(access->GetInterface(IID_PPV_ARGS(tex.GetAddressOf())))) return;

                    D3D11_TEXTURE2D_DESC desc{};
                    tex->GetDesc(&desc);

                    std::lock_guard<std::mutex> lock(m_mutex);
                    if (!raw->latest || raw->texW != desc.Width || raw->texH != desc.Height) {
                        D3D11_TEXTURE2D_DESC own = desc;
                        own.Usage = D3D11_USAGE_DEFAULT;
                        own.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                        own.CPUAccessFlags = 0;
                        own.MiscFlags = 0;
                        ComPtr<ID3D11Texture2D> created;
                        if (FAILED(m_d3d->CreateTexture2D(&own, nullptr, created.GetAddressOf())))
                            return;
                        raw->latest = created;
                        raw->bitmap.Reset(); // re-wrapped on the render thread
                        raw->texW = desc.Width;
                        raw->texH = desc.Height;
                    }
                    {
                        // The immediate context is not thread-safe; the D2D
                        // multithread lock serializes us against rendering.
                        ComPtr<ID2D1Multithread> mt;
                        m_factory->QueryInterface(IID_PPV_ARGS(mt.GetAddressOf()));
                        if (mt) mt->Enter();
                        ComPtr<ID3D11DeviceContext> ctx;
                        m_d3d->GetImmediateContext(ctx.GetAddressOf());
                        ctx->CopyResource(raw->latest.Get(), tex.Get());
                        if (mt) mt->Leave();
                    }
                    if (!raw->announced) {
                        raw->announced = true;
                        PostMessageW(mainWnd, IDM_WM_PREVIEW_FRAME, 0, (LPARAM)target);
                    }
                } catch (...) {
                    // Frame pool raced with session close; drop the frame.
                }
            });

        entry->session.StartCapture();
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_entries.push_back(std::move(entry));
        }
        return true;
    } catch (...) {
        return false;
    }
}

void PreviewManager::Stop(HWND target) {
    std::unique_ptr<PreviewEntry> victim;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
            if ((*it)->hwnd == target) {
                victim = std::move(*it);
                m_entries.erase(it);
                break;
            }
        }
    }
    if (victim) victim->Close(); // outside the lock: Close waits for callbacks
}

void PreviewManager::StopAll() {
    std::vector<std::unique_ptr<PreviewEntry>> victims;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        victims.swap(m_entries);
    }
    for (auto& v : victims) v->Close();
}

ID2D1Bitmap1* PreviewManager::AcquireBitmap(HWND target) {
    // Caller holds LockForDraw().
    for (auto& e : m_entries) {
        if (e->hwnd != target) continue;
        if (!e->latest) return nullptr;
        if (!e->bitmap) {
            ComPtr<IDXGISurface> surf;
            if (FAILED(e->latest.As(&surf))) return nullptr;
            D2D1_BITMAP_PROPERTIES1 bp = D2D1::BitmapProperties1(
                D2D1_BITMAP_OPTIONS_NONE,
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
            if (FAILED(m_dc->CreateBitmapFromDxgiSurface(surf.Get(), &bp,
                                                         e->bitmap.GetAddressOf())))
                return nullptr;
        }
        return e->bitmap.Get();
    }
    return nullptr;
}

std::unique_lock<std::mutex> PreviewManager::LockForDraw() {
    return std::unique_lock<std::mutex>(m_mutex);
}
