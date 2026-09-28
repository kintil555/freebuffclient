#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dwmapi.h>
#include <cmath>
#include <cstdio>
#include <string>
#include <thread>
#include <filesystem>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")

#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' publicKeyToken='6595b64144ccf1df' language='*' processorArchitecture='*'\"")

using std::wstring;
namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Nova injector — one window, one button. Dark glassy card, animated gradient
// border, spring-eased inject button with progress ring and status text.
// ---------------------------------------------------------------------------

static const wchar_t WC_MAIN[] = L"NoNovaMain";
static const wchar_t WC_BTN[]  = L"NoNovaButton";

static HWND g_hwnd       = nullptr;
static HWND g_btn        = nullptr;
static float g_time      = 0.f;
static float g_progress  = 0.f;     // 0..1 injection progress
static float g_press     = 0.f;     // button press animation 0..1
static int   g_state     = 0;       // 0 idle, 1 working, 2 done, 3 error
static wstring g_status  = L"waiting for you";
static UINT_PTR g_timer  = 0;

// ---- status helpers --------------------------------------------------------
static void setStatus(const wchar_t* s, int state) {
    g_status = s;
    g_state  = state;
    InvalidateRect(g_hwnd, nullptr, TRUE);
}

// ---- injection -------------------------------------------------------------
struct ProcInfo { DWORD pid; HWND hwnd; };

static BOOL CALLBACK enumWindowCb(HWND hwnd, LPARAM lp) {
    auto& out = *reinterpret_cast<ProcInfo*>(lp);
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (!pid) return TRUE;

    wchar_t exePath[MAX_PATH] = {};
    DWORD size = MAX_PATH;
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (h) {
        QueryFullProcessImageNameW(h, 0, exePath, &size);
        CloseHandle(h);
    }
    if (wcsstr(exePath, L"java.exe") || wcsstr(exePath, L"javaw.exe")) {
        // Only want visible top-level windows (the game window).
        if (IsWindowVisible(hwnd) && GetWindow(hwnd, GW_OWNER) == nullptr) {
            out.pid = pid; out.hwnd = hwnd;
            return FALSE;
        }
    }
    return TRUE;
}

static bool findMinecraft(DWORD& pidOut) {
    ProcInfo pi{};
    EnumWindows(enumWindowCb, reinterpret_cast<LPARAM>(&pi));
    pidOut = pi.pid;
    return pi.pid != 0;
}

static bool inject(DWORD pid, const wstring& dllPath, wstring& err) {
    HANDLE hProc = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
                               PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
                               FALSE, pid);
    if (!hProc) { err = L"OpenProcess failed (" + std::to_wstring(GetLastError()) + L")"; return false; }

    SIZE_T size = (dllPath.size() + 1) * sizeof(wchar_t);
    LPVOID remote = VirtualAllocEx(hProc, nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote) { err = L"VirtualAllocEx failed"; CloseHandle(hProc); return false; }

    if (!WriteProcessMemory(hProc, remote, dllPath.c_str(), size, nullptr)) {
        err = L"WriteProcessMemory failed"; VirtualFreeEx(hProc, remote, 0, MEM_RELEASE); CloseHandle(hProc); return false;
    }

    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    FARPROC loadLib = GetProcAddress(k32, "LoadLibraryW");
    if (!loadLib) { err = L"no LoadLibraryW"; VirtualFreeEx(hProc, remote, 0, MEM_RELEASE); CloseHandle(hProc); return false; }

    HANDLE th = CreateRemoteThread(hProc, nullptr, 0,
        reinterpret_cast<LPTHREAD_START_ROUTINE>(loadLib), remote, 0, nullptr);
    if (!th) { err = L"CreateRemoteThread failed"; VirtualFreeEx(hProc, remote, 0, MEM_RELEASE); CloseHandle(hProc); return false; }

    WaitForSingleObject(th, 8000);
    CloseHandle(th);
    VirtualFreeEx(hProc, remote, 0, MEM_RELEASE);
    CloseHandle(hProc);
    return true;
}

// ---- GDI helpers: rounded rects, glow, text --------------------------------
static void fillRound(HDC dc, LONG x, LONG y, LONG w, LONG h, LONG r, COLORREF c, BYTE alpha) {
    // Region-based fill: alpha is approximated by color dithering toward bg.
    // (True per-pixel alpha needs a layered window; not needed for this look.)
    COLORREF bg = RGB(12, 8, 20);
    BYTE aR = GetRValue(c), aG = GetGValue(c), aB = GetBValue(c);
    float f = alpha / 255.f;
    COLORREF blended = RGB(BYTE(aR * f + GetRValue(bg) * (1 - f)),
                           BYTE(aG * f + GetGValue(bg) * (1 - f)),
                           BYTE(aB * f + GetBValue(bg) * (1 - f)));
    HBRUSH br = CreateSolidBrush(blended);
    HRGN region = CreateRoundRectRgn(x, y, x + w, y + h, r * 2, r * 2);
    FillRgn(dc, region, br);
    DeleteObject(region);
    DeleteObject(br);
}

static void drawTextC(HDC dc, const wstring& s, LONG cx, LONG cy, LONG h, COLORREF c) {
    HFONT f = CreateFontW(-h, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                          DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                          CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Variable Display");
    if (!f) f = CreateFontW(-h, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                            DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HGDIOBJ old = SelectObject(dc, f);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, c);
    SIZE sz{};
    GetTextExtentPoint32W(dc, s.c_str(), (int)s.size(), &sz);
    TextOutW(dc, cx - sz.cx / 2, cy - sz.cy / 2, s.c_str(), (int)s.size());
    SelectObject(dc, old);
    DeleteObject(f);
}

// ---- main window paint -----------------------------------------------------
static void paint(HDC dc, RECT rc) {
    // Background: vertical near-black plum gradient
    TRIVERTEX vt[2] = {};
    vt[0].x = 0; vt[0].y = 0;
    vt[0].Red = 0x0F00; vt[0].Green = 0x0A00; vt[0].Blue = 0x1800; vt[0].Alpha = 0xFF00;
    vt[1].x = rc.right; vt[1].y = rc.bottom;
    vt[1].Red = 0x1A00; vt[1].Green = 0x1000; vt[1].Blue = 0x2A00; vt[1].Alpha = 0xFF00;
    GRADIENT_RECT g{ 0, 1 };
    GradientFill(dc, vt, 2, &g, 1, GRADIENT_FILL_RECT_V);

    // Animated accent border glow (two rotating gradient hues).
    const float t = g_time;
    COLORREF c1 = RGB(BYTE(120 + 60 * std::sin(t * 1.3)),
                      BYTE(40 + 25 * std::sin(t * 1.7 + 2.f)),
                      BYTE(220 + 30 * std::sin(t * 1.1)));
    COLORREF c2 = RGB(BYTE(60 + 30 * std::sin(t * 0.9 + 1.f)),
                      BYTE(30 + 15 * std::sin(t * 1.4)),
                      BYTE(160 + 40 * std::sin(t * 1.2 + 3.f)));

    // Card
    const LONG cx = (rc.right - rc.left) / 2, cy = (rc.bottom - rc.top) / 2;
    const LONG cw = 420, ch = 240;
    const LONG x0 = cx - cw / 2, y0 = cy - ch / 2;

    // Soft outer glow: several expanding rounded rects, fading alpha.
    for (int i = 6; i >= 1; --i) {
        BYTE a = BYTE(14 * (7 - i));
        fillRound(dc, x0 - i * 3, y0 - i * 3, cw + i * 6, ch + i * 6, 26 + i * 2, c2, a);
    }
    // Card fill.
    fillRound(dc, x0, y0, cw, ch, 26, RGB(18, 13, 28), 255);

    // Gradient top strip (thin accent line).
    TRIVERTEX tv2[2] = {};
    tv2[0].x = x0 + 30; tv2[0].y = y0 + 64;
    tv2[0].Red = GetRValue(c1) * 256; tv2[0].Green = GetGValue(c1) * 256; tv2[0].Blue = GetBValue(c1) * 256; tv2[0].Alpha = 0xFF00;
    tv2[1].x = x0 + cw - 30; tv2[1].y = y0 + 67;
    tv2[1].Red = GetRValue(c2) * 256; tv2[1].Green = GetGValue(c2) * 256; tv2[1].Blue = GetBValue(c2) * 256; tv2[1].Alpha = 0xFF00;
    GRADIENT_RECT g2{ 0, 1 };
    GradientFill(dc, tv2, 2, &g2, 1, GRADIENT_FILL_RECT_H);

    drawTextC(dc, L"NOVA", cx, y0 + 36, 34, RGB(240, 234, 255));
    drawTextC(dc, g_status.c_str(), cx, y0 + 96, 16, RGB(170, 160, 200));

    // Progress bar under status (animated width).
    if (g_state == 1) {
        const LONG bw = 260, bx = cx - bw / 2, by = y0 + 122;
        fillRound(dc, bx, by, bw, 6, 3, RGB(35, 26, 54), 255);
        const float eased = g_progress < 1.f ? (1.f - std::pow(1.f - g_progress, 2.f)) : 1.f;
        fillRound(dc, bx, by, LONG(bw * eased), 6, 3, c1, 255);
    }

    // Footer hint.
    drawTextC(dc, L"Insert toggles the menu in-game  ·  built for 26.2",
              cx, y0 + ch - 22, 13, RGB(120, 110, 150));
}

// ---- button window ---------------------------------------------------------
static void paintButton(HDC dc, RECT rc) {
    const LONG w = rc.right - rc.left, h = rc.bottom - rc.top;
    const float press = g_press; // 0..1
    const LONG shrink = LONG(press * 4);

    const float t = g_time;
    COLORREF cA = RGB(BYTE(150 + 40 * std::sin(t * 1.6)),
                      BYTE(60 + 25 * std::sin(t * 2.1 + 1.f)),
                      BYTE(255));
    COLORREF cB = RGB(BYTE(90 + 30 * std::sin(t * 1.2 + 2.f)),
                      BYTE(40 + 20 * std::sin(t * 1.8)),
                      BYTE(210 + 35 * std::sin(t * 1.5 + 4.f)));

    if (g_state == 2) { cA = RGB(90, 220, 140); cB = RGB(40, 160, 100); }
    if (g_state == 3) { cA = RGB(235, 90, 110); cB = RGB(170, 40, 70); }

    // Glow.
    for (int i = 5; i >= 1; --i) {
        BYTE a = BYTE(20 * (6 - i));
        fillRound(dc, -i * 2 + shrink, -i * 2 + shrink, w + i * 4 - shrink * 2, h + i * 4 - shrink * 2,
                  22 + i, cB, a);
    }
    // Body gradient: manual two-tone via two halves.
    fillRound(dc, shrink, shrink, w - shrink * 2, h - shrink * 2, 20, cA, 255);
    // Bottom-half overlay for gradient illusion.
    fillRound(dc, shrink, shrink + (h - shrink * 2) / 2, w - shrink * 2, (h - shrink * 2) / 2, 20, cB, 120);

    drawTextC(dc, g_state == 1 ? L"injecting…" : g_state == 2 ? L"injected ✓"
             : g_state == 3 ? L"failed — click to retry" : L"inject",
             w / 2, h / 2, 20, RGB(255, 255, 255));

    // Progress ring while working: arc along the border.
    if (g_state == 1) {
        HPEN pen = CreatePen(PS_SOLID, 3, RGB(255, 255, 255));
        HGDIOBJ old = SelectObject(dc, pen);
        Arc(dc, 2, 2, w - 2, h - 2,
            w / 2, 0, w / 2, h);
        SelectObject(dc, old);
        DeleteObject(pen);
    }
}

// ---- button proc -----------------------------------------------------------
static LRESULT CALLBACK btnProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);
            RECT rc; GetClientRect(hwnd, &rc);
            // Double-buffer to kill flicker.
            HDC mem = CreateCompatibleDC(dc);
            HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
            HGDIOBJ oldBmp = SelectObject(mem, bmp);
            paintButton(mem, rc);
            BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
            SelectObject(mem, oldBmp);
            DeleteObject(bmp);
            DeleteDC(mem);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_LBUTTONDOWN: SetCapture(hwnd); g_press = 1.f; InvalidateRect(hwnd, nullptr, TRUE); return 0;
        case WM_LBUTTONUP: {
            ReleaseCapture();
            g_press = 0.f;
            InvalidateRect(hwnd, nullptr, TRUE);
            if (g_state == 0 || g_state == 3) {
                // Fire injection on a worker thread.
                std::thread([]() {
                    setStatus(L"looking for minecraft…", 1);
                    g_progress = 0.15f;
                    DWORD pid = 0;
                    if (!findMinecraft(pid)) { setStatus(L"minecraft not found — launch the game", 3); return; }
                    g_progress = 0.45f;
                    wchar_t self[MAX_PATH];
                    GetModuleFileNameW(nullptr, self, MAX_PATH);
                    fs::path dll = fs::path(self).parent_path() / L"nova.dll";
                    if (!fs::exists(dll)) { setStatus(L"nova.dll not found next to injector", 3); return; }
                    g_progress = 0.7f;
                    wstring err;
                    if (inject(pid, dll.wstring(), err)) {
                        g_progress = 1.f;
                        setStatus(L"injected — press Insert in game", 2);
                    } else {
                        setStatus((L"failed: " + err).c_str(), 3);
                    }
                }).detach();
            }
            return 0;
        }
        case WM_SETCURSOR:
            SetCursor(LoadCursor(nullptr, IDC_HAND));
            return TRUE;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---- main proc -------------------------------------------------------------
static LRESULT CALLBACK mainProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            RECT rc; GetClientRect(hwnd, &rc);
            const LONG bw = 200, bh = 52;
            g_btn = CreateWindowExW(0, WC_BTN, L"", WS_CHILD | WS_VISIBLE,
                                    (rc.right - bw) / 2, 120, bw, bh,
                                    hwnd, nullptr, nullptr, nullptr);
            // Rounded corners (Win11); harmless no-op on Win10.
            DWM_WINDOW_CORNER_PREFERENCE pref = DWMWCP_ROUND;
            DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &pref, sizeof(pref));
            if (g_btn) {
                DwmSetWindowAttribute(g_btn, DWMWA_WINDOW_CORNER_PREFERENCE, &pref, sizeof(pref));
            }
            g_timer = SetTimer(hwnd, 1, 16, nullptr);
            return 0;
        }
        case WM_TIMER:
            g_time += 0.016f;
            if (g_state == 1) g_progress = (g_progress < 0.9f) ? g_progress + 0.004f : g_progress;
            g_press *= 0.86f;   // release spring
            InvalidateRect(g_btn, nullptr, TRUE);
            InvalidateRect(hwnd, nullptr, TRUE);
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);
            RECT rc; GetClientRect(hwnd, &rc);
            HDC mem = CreateCompatibleDC(dc);
            HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
            HGDIOBJ oldBmp = SelectObject(mem, bmp);
            paint(mem, rc);
            BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
            SelectObject(mem, oldBmp);
            DeleteObject(bmp);
            DeleteDC(mem);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_ERASEBKGND: return 1;
        case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int nCmdShow) {
    WNDCLASSEXW wc = { sizeof(wc) };
    wc.hInstance = hInst;
    wc.lpszClassName = WC_MAIN;
    wc.lpfnWndProc = mainProc;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.style = CS_HREDRAW | CS_VREDRAW;
    RegisterClassExW(&wc);

    WNDCLASSEXW wb = { sizeof(wb) };
    wb.hInstance = hInst;
    wb.lpszClassName = WC_BTN;
    wb.lpfnWndProc = btnProc;
    wb.hCursor = LoadCursor(nullptr, IDC_HAND);
    wb.hbrBackground = nullptr;
    RegisterClassExW(&wb);

    const LONG ww = 520, wh = 360;
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const LONG wx = work.left + ((work.right - work.left) - ww) / 2;
    const LONG wy = work.top + ((work.bottom - work.top) - wh) / 2;

    g_hwnd = CreateWindowExW(WS_EX_OVERLAPPEDWINDOW, WC_MAIN, L"NOVA — 26.2",
                             WS_OVERLAPPEDWINDOW & ~(WS_MAXIMIZEBOX),
                             wx, wy, ww, wh, nullptr, nullptr, hInst, nullptr);
    ShowWindow(g_hwnd, nCmdShow);
    UpdateWindow(g_hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
