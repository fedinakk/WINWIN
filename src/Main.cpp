// InfiniteDesk entry point.

#include "App.h"

#include <winrt/base.h>

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int) {
    // Per-monitor DPI v2: everything in this app works in physical pixels;
    // no double-application of DPI on multi-monitor moves. The manifest sets
    // this too - the call is a belt-and-braces fallback.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // Single instance: a second copy would fight over the mouse hook.
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"InfiniteDesk.SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(nullptr, L"InfiniteDesk уже запущен (см. значок в трее).",
                    L"InfiniteDesk", MB_OK | MB_ICONINFORMATION);
        return 0;
    }

    winrt::init_apartment(winrt::apartment_type::single_threaded);

    int rc = 1;
    {
        App app;
        if (app.Init(hInstance)) {
            rc = app.Run();
        } else {
            MessageBoxW(nullptr,
                        L"Не удалось инициализировать InfiniteDesk\n"
                        L"(DirectComposition/Direct2D или хук мыши).",
                        L"InfiniteDesk", MB_OK | MB_ICONERROR);
        }
        app.Shutdown();
    }

    winrt::uninit_apartment();
    if (mutex) CloseHandle(mutex);
    return rc;
}
