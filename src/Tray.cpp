#include "Tray.h"

bool TrayIcon::Create(HWND ownerWnd) {
    m_owner = ownerWnd;
    NOTIFYICONDATAW nid{ sizeof(nid) };
    nid.hWnd = ownerWnd;
    nid.uID = 1;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = IDM_WM_TRAY;
    nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(nid.szTip, L"InfiniteDesk — бесконечный холст (ПКМ: меню)");
    m_added = Shell_NotifyIconW(NIM_ADD, &nid) != FALSE;
    return m_added;
}

void TrayIcon::Destroy() {
    if (!m_added) return;
    NOTIFYICONDATAW nid{ sizeof(nid) };
    nid.hWnd = m_owner;
    nid.uID = 1;
    Shell_NotifyIconW(NIM_DELETE, &nid);
    m_added = false;
}

void TrayIcon::OnCallback(WPARAM, LPARAM lp, bool fpsShown) {
    if (LOWORD(lp) != WM_RBUTTONUP && LOWORD(lp) != WM_CONTEXTMENU) return;

    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, IDC_TRAY_ZOOM_FIT, L"Обзор всех окон (Ctrl+Alt+W)");
    AppendMenuW(menu, MF_STRING, IDC_TRAY_RESET, L"Сброс камеры (Ctrl+Alt+0)");
    AppendMenuW(menu, MF_STRING, IDC_TRAY_GATHER, L"Собрать окна на экран");
    AppendMenuW(menu, MF_STRING | (fpsShown ? MF_CHECKED : 0), IDC_TRAY_TOGGLE_FPS,
                L"FPS-оверлей (Ctrl+Alt+F)");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IDC_TRAY_EXIT, L"Выход (Ctrl+Alt+Q)");

    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(m_owner); // required for the menu to dismiss properly
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, m_owner, nullptr);
    PostMessageW(m_owner, WM_NULL, 0, 0);
    DestroyMenu(menu);
}
