#pragma once
// config.ini loading/saving. The file lives next to the exe; missing file or
// missing keys fall back to defaults, and a default file is written out.

#include "Common.h"

struct HotkeyBinding {
    UINT modifiers = 0; // MOD_CONTROL | ...
    UINT vk = 0;
    bool Valid() const { return vk != 0; }
};

struct BookmarkSlot {
    bool set = false;
    double x = 0.0, y = 0.0, scale = 1.0;
};

struct Config {
    // [Camera]
    double minScale = 0.05;
    double maxScale = 2.0;
    double zoomStep = 1.12;
    double flyDurationMs = 350.0;

    // [Input]
    double inertiaFriction = 4.0;
    double inertiaMinSpeed = 120.0;

    // [Snap]
    bool snapEnabled = true;
    double snapDistance = 16.0;
    double snapReleaseFactor = 1.8;
    double snapGap = 0.0;

    // [Render]
    int targetFps = 240;
    double gridStep = 96.0;
    uint32_t backgroundColor = 0xFF1A1B26;
    uint32_t gridDotColor = 0xFF2F3347;
    bool drawWallpaper = true;

    // [Preview]
    bool previewEnabled = true;
    double previewBelowScale = 0.45;
    double previewAboveScale = 0.55;

    // [Debug]
    bool showFps = false;

    // [Hotkeys] name -> binding
    std::unordered_map<std::wstring, HotkeyBinding> hotkeys;

    // [Bookmarks]
    BookmarkSlot bookmarks[4];

    std::wstring path; // full path of config.ini

    void Load();          // reads config.ini (creating it from defaults if absent)
    void SaveBookmarks(); // persists bookmark slots + ShowFps back into the file
};

// Parses "Ctrl+Alt+Shift+W" -> binding. Returns invalid binding on parse failure.
HotkeyBinding ParseHotkey(const std::wstring& text);
