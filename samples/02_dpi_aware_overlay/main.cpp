// Sample 02: DPI-aware transparent topmost overlay that paints a D2D circle
// tracking the mouse cursor — mirrors the pattern in IconOverlayManager::drawOverlay().

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d2d1.h>
#include <d2d1helper.h>
#include <wrl/client.h>
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "dwmapi.lib")

using Microsoft::WRL::ComPtr;

static ComPtr<ID2D1Factory>         g_d2dFactory;
static ComPtr<ID2D1DCRenderTarget>  g_renderTarget;

static void render(HWND hwnd) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    int w = rc.right, h = rc.bottom;
    if (w <= 0 || h <= 0) return;

    HDC hdc = GetDC(hwnd);
    HDC memDC = CreateCompatibleDC(hdc);

    BITMAPINFO bmi{};
    bmi.bmiHeader = { sizeof(bmi.bmiHeader), w, -h, 1, 32, BI_RGB };
    void* bits = nullptr;
    HBITMAP hBmp = CreateDIBSection(memDC, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HBITMAP hOld = static_cast<HBITMAP>(SelectObject(memDC, hBmp));

    if (g_renderTarget) {
        RECT drawRc{ 0, 0, w, h };
        g_renderTarget->BindDC(memDC, &drawRc);
        g_renderTarget->BeginDraw();
        g_renderTarget->Clear(D2D1::ColorF(0, 0, 0, 0));

        POINT cur;
        GetCursorPos(&cur);
        ScreenToClient(hwnd, &cur);

        ComPtr<ID2D1SolidColorBrush> brush;
        g_renderTarget->CreateSolidColorBrush(
            D2D1::ColorF(0.2f, 0.65f, 1.0f, 0.75f), &brush);
        g_renderTarget->FillEllipse(
            D2D1::Ellipse(D2D1::Point2F((float)cur.x, (float)cur.y), 24.0f, 24.0f),
            brush.Get());
        g_renderTarget->EndDraw();
    }

    POINT ptSrc{ 0, 0 }, ptDst;
    RECT wr; GetWindowRect(hwnd, &wr);
    ptDst = { wr.left, wr.top };
    SIZE sz{ w, h };
    BLENDFUNCTION bf{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    UpdateLayeredWindow(hwnd, hdc, &ptDst, &sz, memDC, &ptSrc, 0, &bf, ULW_ALPHA);

    SelectObject(memDC, hOld);
    DeleteObject(hBmp);
    DeleteDC(memDC);
    ReleaseDC(hwnd, hdc);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_MOUSEMOVE:
    case WM_TIMER:
        render(hwnd);
        return 0;
    case WM_DPICHANGED: {
        auto* rc = reinterpret_cast<RECT*>(lp);
        SetWindowPos(hwnd, nullptr, rc->left, rc->top,
                     rc->right - rc->left, rc->bottom - rc->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_NCHITTEST: return HTTRANSPARENT;
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) PostQuitMessage(0);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, g_d2dFactory.GetAddressOf());
    D2D1_RENDER_TARGET_PROPERTIES rtp = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    g_d2dFactory->CreateDCRenderTarget(&rtp, g_renderTarget.GetAddressOf());

    WNDCLASSEXW wc{ .cbSize = sizeof(wc), .lpfnWndProc = WndProc,
                    .hInstance = hInst, .lpszClassName = L"Sample02" };
    RegisterClassExW(&wc);

    int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    HWND hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
        L"Sample02", nullptr, WS_POPUP, 0, 0, sw, sh,
        nullptr, nullptr, hInst, nullptr);

    ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    SetTimer(hwnd, 1, 16, nullptr); // ~60fps refresh for cursor tracking

    // Press Escape to exit.
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
