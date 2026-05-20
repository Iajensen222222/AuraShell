// Sample 01: Minimal AuraShell-style Win32 window with Mica backdrop + dark mode.
// Demonstrates the minimum viable setup for any AuraShell UI component.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

#ifndef DWMWA_SYSTEMBACKDROP_TYPE
#define DWMWA_SYSTEMBACKDROP_TYPE 38
#endif
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
static constexpr int DWMSBT_MAINWINDOW = 2; // Mica

static bool isDarkModeEnabled() {
    DWORD val = 1;
    DWORD size = sizeof(val);
    RegGetValueW(HKEY_CURRENT_USER,
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &val, &size);
    return val == 0; // 0 = dark, 1 = light
}

static void applyDwm(HWND hwnd) {
    BOOL dark = isDarkModeEnabled() ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    int backdrop = DWMSBT_MAINWINDOW;
    DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof(backdrop));
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_ERASEBKGND: return 1; // let Mica paint the background
    case WM_SETTINGCHANGE:
        applyDwm(hwnd); // re-apply when user switches dark/light mode
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int nShow) {
    WNDCLASSEXW wc{ .cbSize = sizeof(wc), .lpfnWndProc = WndProc,
                    .hInstance = hInst, .hCursor = LoadCursor(nullptr, IDC_ARROW),
                    .hbrBackground = nullptr, .lpszClassName = L"Sample01" };
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(0, L"Sample01", L"AuraShell Sample 01 — Basic Window",
                                WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                                800, 600, nullptr, nullptr, hInst, nullptr);
    applyDwm(hwnd);
    ShowWindow(hwnd, nShow);
    UpdateWindow(hwnd);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
