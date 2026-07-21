#pragma once
// Stretch feature: live window previews via Windows.Graphics.Capture.
//
// When the camera zooms out past a threshold, real windows are parked
// offscreen and the canvas draws live captured frames at the right virtual
// rects instead - the closest Windows allows to driftwm's "zoom out and see
// everything". Zooming back in (or clicking a preview) restores the real
// windows.
//
// Threading: frame pools are free-threaded; FrameArrived runs on capture
// worker threads. The worker only copies the frame texture into a per-window
// texture, guarded by the D2D multithread lock plus our own mutex. D2D
// bitmaps are (re)created lazily on the render thread under the same mutex.
//
// Every WinRT object is released in Stop()/Shutdown(); a window closing mid-
// capture surfaces as item.Closed or a failed TryGetNextFrame and simply
// drops that entry.

#include "Common.h"

#include <memory>
#include <mutex>

struct ID3D11Device;
struct ID2D1DeviceContext;
struct ID2D1Factory1;
struct ID2D1Bitmap1;

class PreviewEntry; // hides all C++/WinRT types inside the .cpp

class PreviewManager {
public:
    // Returns false when WGC is unavailable; the app then keeps geometric
    // zoom only (documented degradation, not a broken stub).
    bool Init(HWND mainWnd, ID3D11Device* d3d, ID2D1Factory1* factory, ID2D1DeviceContext* dc);
    void Shutdown();
    bool Available() const { return m_available; }

    // Begin capturing a window. Posts IDM_WM_PREVIEW_FRAME (lParam = hwnd) to
    // the main window when the first frame lands, so the app can park the
    // real window only once its live image exists (no blink).
    bool Start(HWND target);
    void Stop(HWND target);
    void StopAll();
    bool Capturing(HWND target) const;

    // Render-thread access to the latest frame; returns null before the first
    // frame. Valid until the next Stop/Shutdown for that window.
    ID2D1Bitmap1* AcquireBitmap(HWND target);

    // Must wrap AcquireBitmap + drawing (keeps the copy thread out).
    std::unique_lock<std::mutex> LockForDraw();

private:
    HWND m_mainWnd = nullptr;
    bool m_available = false;
    ID3D11Device* m_d3d = nullptr;
    ID2D1DeviceContext* m_dc = nullptr;
    ID2D1Factory1* m_factory = nullptr;
    std::mutex m_mutex;
    std::vector<std::unique_ptr<PreviewEntry>> m_entries;
    // Closed entries are kept until Shutdown: a FrameArrived callback already
    // in flight on a capture worker may still touch its entry briefly.
    std::vector<std::unique_ptr<PreviewEntry>> m_graveyard;
};
