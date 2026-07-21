#include "Config.h"

namespace {

std::wstring ExeDir() {
    wchar_t buf[MAX_PATH]{};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring s = buf;
    size_t pos = s.find_last_of(L"\\/");
    return pos == std::wstring::npos ? s : s.substr(0, pos);
}

double GetIniDouble(const std::wstring& file, const wchar_t* section, const wchar_t* key, double def) {
    wchar_t buf[64]{};
    GetPrivateProfileStringW(section, key, L"", buf, 64, file.c_str());
    if (!buf[0]) return def;
    wchar_t* end = nullptr;
    double v = wcstod(buf, &end);
    return end == buf ? def : v;
}

int GetIniInt(const std::wstring& file, const wchar_t* section, const wchar_t* key, int def) {
    return (int)GetPrivateProfileIntW(section, key, def, file.c_str());
}

uint32_t GetIniColor(const std::wstring& file, const wchar_t* section, const wchar_t* key, uint32_t def) {
    wchar_t buf[32]{};
    GetPrivateProfileStringW(section, key, L"", buf, 32, file.c_str());
    if (!buf[0]) return def;
    wchar_t* end = nullptr;
    uint32_t v = (uint32_t)wcstoul(buf, &end, 16);
    return end == buf ? def : v;
}

std::wstring GetIniStr(const std::wstring& file, const wchar_t* section, const wchar_t* key, const wchar_t* def) {
    wchar_t buf[128]{};
    GetPrivateProfileStringW(section, key, def, buf, 128, file.c_str());
    return buf;
}

UINT KeyNameToVk(const std::wstring& name) {
    if (name.size() == 1) {
        wchar_t c = towupper(name[0]);
        if ((c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9')) return (UINT)c;
    }
    if (name.size() >= 2 && (name[0] == L'F' || name[0] == L'f')) {
        int n = _wtoi(name.c_str() + 1);
        if (n >= 1 && n <= 24) return VK_F1 + n - 1;
    }
    struct { const wchar_t* n; UINT vk; } names[] = {
        {L"HOME", VK_HOME}, {L"END", VK_END}, {L"LEFT", VK_LEFT}, {L"RIGHT", VK_RIGHT},
        {L"UP", VK_UP}, {L"DOWN", VK_DOWN}, {L"SPACE", VK_SPACE}, {L"TAB", VK_TAB},
        {L"ESC", VK_ESCAPE}, {L"ESCAPE", VK_ESCAPE}, {L"PGUP", VK_PRIOR}, {L"PGDN", VK_NEXT},
        {L"INS", VK_INSERT}, {L"DEL", VK_DELETE}, {L"ENTER", VK_RETURN}, {L"BACKSPACE", VK_BACK},
    };
    std::wstring up = name;
    for (auto& c : up) c = towupper(c);
    for (auto& e : names) if (up == e.n) return e.vk;
    return 0;
}

const wchar_t* kHotkeyNames[] = {
    L"ZoomToFit", L"Home", L"CenterWindow", L"ResetZoom", L"ToggleFps", L"Exit",
    L"Bookmark1", L"Bookmark2", L"Bookmark3", L"Bookmark4",
    L"SaveBookmark1", L"SaveBookmark2", L"SaveBookmark3", L"SaveBookmark4",
};

const wchar_t* kHotkeyDefaults[] = {
    L"Ctrl+Alt+W", L"Ctrl+Alt+Home", L"Ctrl+Alt+C", L"Ctrl+Alt+0", L"Ctrl+Alt+F", L"Ctrl+Alt+Q",
    L"Ctrl+Alt+1", L"Ctrl+Alt+2", L"Ctrl+Alt+3", L"Ctrl+Alt+4",
    L"Ctrl+Alt+Shift+1", L"Ctrl+Alt+Shift+2", L"Ctrl+Alt+Shift+3", L"Ctrl+Alt+Shift+4",
};

const wchar_t kDefaultIni[] =
L"; InfiniteDesk config. See README.md for the full key reference.\r\n"
L"[Camera]\r\n"
L"MinScale=0.05\r\nMaxScale=2.0\r\nZoomStep=1.12\r\nFlyDurationMs=350\r\n"
L"[Input]\r\n"
L"InertiaFriction=4.0\r\nInertiaMinSpeed=120\r\n"
L"[Snap]\r\n"
L"Enabled=1\r\nSnapDistance=16\r\nReleaseFactor=1.8\r\nGap=0\r\n"
L"[Render]\r\n"
L"TargetFPS=240\r\nGridStep=96\r\nBackgroundColor=FF1A1B26\r\nGridDotColor=FF2F3347\r\nDrawWallpaper=1\r\n"
L"[Preview]\r\n"
L"Enabled=1\r\nBelowScale=0.45\r\nAboveScale=0.55\r\n"
L"[Debug]\r\n"
L"ShowFps=0\r\n"
L"[Hotkeys]\r\n"
L"ZoomToFit=Ctrl+Alt+W\r\nHome=Ctrl+Alt+Home\r\nCenterWindow=Ctrl+Alt+C\r\nResetZoom=Ctrl+Alt+0\r\n"
L"ToggleFps=Ctrl+Alt+F\r\nExit=Ctrl+Alt+Q\r\n"
L"Bookmark1=Ctrl+Alt+1\r\nBookmark2=Ctrl+Alt+2\r\nBookmark3=Ctrl+Alt+3\r\nBookmark4=Ctrl+Alt+4\r\n"
L"SaveBookmark1=Ctrl+Alt+Shift+1\r\nSaveBookmark2=Ctrl+Alt+Shift+2\r\n"
L"SaveBookmark3=Ctrl+Alt+Shift+3\r\nSaveBookmark4=Ctrl+Alt+Shift+4\r\n"
L"[Bookmarks]\r\n";

} // namespace

HotkeyBinding ParseHotkey(const std::wstring& text) {
    HotkeyBinding b{};
    size_t start = 0;
    std::wstring keyPart;
    while (start <= text.size()) {
        size_t plus = text.find(L'+', start);
        std::wstring part = text.substr(start, plus == std::wstring::npos ? std::wstring::npos : plus - start);
        // trim
        while (!part.empty() && iswspace(part.front())) part.erase(part.begin());
        while (!part.empty() && iswspace(part.back())) part.pop_back();
        std::wstring up = part;
        for (auto& c : up) c = towupper(c);
        if (up == L"CTRL" || up == L"CONTROL") b.modifiers |= MOD_CONTROL;
        else if (up == L"ALT") b.modifiers |= MOD_ALT;
        else if (up == L"SHIFT") b.modifiers |= MOD_SHIFT;
        else if (up == L"WIN") b.modifiers |= MOD_WIN;
        else keyPart = part;
        if (plus == std::wstring::npos) break;
        start = plus + 1;
    }
    b.vk = KeyNameToVk(keyPart);
    if (!b.vk) b.modifiers = 0;
    return b;
}

void Config::Load() {
    path = ExeDir() + L"\\config.ini";

    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        // Write a default config (UTF-16 LE with BOM so WritePrivateProfile works later).
        HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            DWORD written = 0;
            const wchar_t bom = 0xFEFF;
            WriteFile(h, &bom, sizeof(bom), &written, nullptr);
            WriteFile(h, kDefaultIni, (DWORD)(wcslen(kDefaultIni) * sizeof(wchar_t)), &written, nullptr);
            CloseHandle(h);
        }
    }

    minScale = GetIniDouble(path, L"Camera", L"MinScale", minScale);
    maxScale = GetIniDouble(path, L"Camera", L"MaxScale", maxScale);
    zoomStep = GetIniDouble(path, L"Camera", L"ZoomStep", zoomStep);
    flyDurationMs = GetIniDouble(path, L"Camera", L"FlyDurationMs", flyDurationMs);

    inertiaFriction = GetIniDouble(path, L"Input", L"InertiaFriction", inertiaFriction);
    inertiaMinSpeed = GetIniDouble(path, L"Input", L"InertiaMinSpeed", inertiaMinSpeed);

    snapEnabled = GetIniInt(path, L"Snap", L"Enabled", snapEnabled ? 1 : 0) != 0;
    snapDistance = GetIniDouble(path, L"Snap", L"SnapDistance", snapDistance);
    snapReleaseFactor = GetIniDouble(path, L"Snap", L"ReleaseFactor", snapReleaseFactor);
    snapGap = GetIniDouble(path, L"Snap", L"Gap", snapGap);

    targetFps = std::clamp(GetIniInt(path, L"Render", L"TargetFPS", targetFps), 30, 1000);
    gridStep = std::clamp(GetIniDouble(path, L"Render", L"GridStep", gridStep), 16.0, 2048.0);
    backgroundColor = GetIniColor(path, L"Render", L"BackgroundColor", backgroundColor);
    gridDotColor = GetIniColor(path, L"Render", L"GridDotColor", gridDotColor);
    drawWallpaper = GetIniInt(path, L"Render", L"DrawWallpaper", drawWallpaper ? 1 : 0) != 0;

    previewEnabled = GetIniInt(path, L"Preview", L"Enabled", previewEnabled ? 1 : 0) != 0;
    previewBelowScale = GetIniDouble(path, L"Preview", L"BelowScale", previewBelowScale);
    previewAboveScale = GetIniDouble(path, L"Preview", L"AboveScale", previewAboveScale);
    if (previewAboveScale < previewBelowScale) previewAboveScale = previewBelowScale + 0.05;

    showFps = GetIniInt(path, L"Debug", L"ShowFps", showFps ? 1 : 0) != 0;

    minScale = Clamp(minScale, 0.01, 1.0);
    maxScale = Clamp(maxScale, 1.0, 8.0);

    for (size_t i = 0; i < _countof(kHotkeyNames); ++i) {
        std::wstring text = GetIniStr(path, L"Hotkeys", kHotkeyNames[i], kHotkeyDefaults[i]);
        HotkeyBinding b = ParseHotkey(text);
        if (!b.Valid()) b = ParseHotkey(kHotkeyDefaults[i]);
        hotkeys[kHotkeyNames[i]] = b;
    }

    for (int i = 0; i < 4; ++i) {
        wchar_t key[16];
        swprintf_s(key, L"Slot%d", i + 1);
        std::wstring v = GetIniStr(path, L"Bookmarks", key, L"");
        if (v.empty()) continue;
        double x, y, s;
        if (swscanf_s(v.c_str(), L"%lf,%lf,%lf", &x, &y, &s) == 3) {
            bookmarks[i] = { true, x, y, s };
        }
    }
}

void Config::SaveBookmarks() {
    for (int i = 0; i < 4; ++i) {
        if (!bookmarks[i].set) continue;
        wchar_t key[16], val[96];
        swprintf_s(key, L"Slot%d", i + 1);
        swprintf_s(val, L"%.2f,%.2f,%.5f", bookmarks[i].x, bookmarks[i].y, bookmarks[i].scale);
        WritePrivateProfileStringW(L"Bookmarks", key, val, path.c_str());
    }
    WritePrivateProfileStringW(L"Debug", L"ShowFps", showFps ? L"1" : L"0", path.c_str());
}
