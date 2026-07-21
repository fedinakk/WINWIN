#pragma once
// Tray icon with the reliability menu: the app holds a global mouse hook and
// paints under everything, so a guaranteed Exit path always exists here.

#include "Common.h"

class TrayIcon {
public:
    bool Create(HWND ownerWnd); // owner receives IDM_WM_TRAY callbacks
    void Destroy();
    // Call from the owner's wndproc on IDM_WM_TRAY; shows the menu on r-click.
    void OnCallback(WPARAM wp, LPARAM lp, bool fpsShown);

private:
    HWND m_owner = nullptr;
    bool m_added = false;
};
