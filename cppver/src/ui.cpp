#include "ui.h"
#include "llm.h"
#include "crypto.h"
#include "settings.h"
#include "resource.h"
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <uxtheme.h>
#include <string>
#include <thread>
#include <vector>
#include <algorithm>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "uxtheme.lib")

// ── Forward declarations ──
static LRESULT CALLBACK MainWndProc(HWND, UINT, WPARAM, LPARAM);
static LRESULT CALLBACK EditSubclassProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
static LRESULT CALLBACK DialogForwardProc(HWND, UINT, WPARAM, LPARAM);

// ── Globals ──
static AppState* g_pApp = nullptr;
static HWND g_hToolbar = nullptr;
static HWND g_hInputHeader = nullptr;
static HWND g_hOutputHeader = nullptr;
static std::vector<HWND> g_toolbarButtons;
static HWND g_hFontLabel = nullptr;
static HWND g_hStatusDot = nullptr;
static HWND g_hStatusText = nullptr;
static HWND g_hFooter = nullptr;
static HWND g_hInputStats = nullptr;
static HWND g_hOutputStats = nullptr;
static HWND g_hConvCount = nullptr;
static HWND g_hProviderLabel = nullptr;
static HWND g_hModelLabel = nullptr;
static HWND g_hOutputCharCount = nullptr;
static HWND g_hToast = nullptr;
static UINT_PTR g_toastTimer = 0;
static HWND g_hDivider = nullptr;
static bool g_dragging = false;

// ── Helpers ──
static const int UI_SCALE = 3;
static int S(int value) { return value * UI_SCALE; }
static int DS(int value) { return value * 2; }

static std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(len, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], len);
    return w;
}

static std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(len, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], len, nullptr, nullptr);
    return s;
}

static std::wstring GetEditText(HWND hEdit) {
    int len = GetWindowTextLengthW(hEdit);
    if (len == 0) return {};
    std::wstring s(len, 0);
    GetWindowTextW(hEdit, &s[0], len + 1);
    return s;
}

static void SetEditText(HWND hEdit, const std::wstring& text) {
    SetWindowTextW(hEdit, text.c_str());
}

// ── Custom themed button ──
static HWND CreateThemeButton(HWND parent, int id, const wchar_t* text,
                               int x, int y, int w, int h) {
    HWND btn = CreateWindowExW(0, L"BUTTON", text,
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        S(x), S(y), S(w), S(h), parent, (HMENU)(INT_PTR)id, nullptr, nullptr);
    return btn;
}

// ── Owner-draw button paint ──
static void PaintButton(DRAWITEMSTRUCT* dis) {
    auto& t = g_pApp->theme;
    bool hover = (dis->itemState & ODS_SELECTED) || (dis->itemState & ODS_HOTLIGHT);
    bool focused = (dis->itemState & ODS_FOCUS);

    COLORREF bg = hover ? t.border : t.surface2;
    COLORREF fg = hover ? t.text : t.textDim;

    // Check if this is the "active" wrap button
    int id = GetDlgCtrlID(dis->hwndItem);
    if (id == IDC_BTN_WRAP && g_pApp->wordWrap) {
        bg = t.accent;
        fg = RGB(255, 255, 255);
    }
    if (id == IDC_BTN_EDIT_OUTPUT && g_pApp->outputEditable) {
        bg = t.accent;
        fg = RGB(255, 255, 255);
    }

    HBRUSH hBr = CreateSolidBrush(bg);
    FillRect(dis->hDC, &dis->rcItem, hBr);
    DeleteObject(hBr);

    // Border
    HPEN hPen = CreatePen(PS_SOLID, 1, t.border);
    HPEN hOld = (HPEN)SelectObject(dis->hDC, hPen);
    HBRUSH hNull = (HBRUSH)GetStockObject(NULL_BRUSH);
    HBRUSH hOldBr = (HBRUSH)SelectObject(dis->hDC, hNull);
    RoundRect(dis->hDC, dis->rcItem.left, dis->rcItem.top,
              dis->rcItem.right, dis->rcItem.bottom, S(10), S(10));
    SelectObject(dis->hDC, hOld);
    SelectObject(dis->hDC, hOldBr);
    DeleteObject(hPen);

    // Text
    SetBkMode(dis->hDC, TRANSPARENT);
    SetTextColor(dis->hDC, fg);
    SelectObject(dis->hDC, g_pApp->hFontSmall);

    wchar_t text[128] = {};
    GetWindowTextW(dis->hwndItem, text, 128);
    DrawTextW(dis->hDC, text, -1, &dis->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

// ── Layout constants ──
static const int TOOLBAR_H = 42 * UI_SCALE;
static const int PANE_HEADER_H = 32 * UI_SCALE;
static const int FOOTER_H = 26 * UI_SCALE;
static const int DIVIDER_W = 4 * UI_SCALE;
static const int TOAST_H = 32 * UI_SCALE;

// ── Resize handler ──
static void LayoutControls(AppState& app) {
    if (!app.hMainWnd) return;
    RECT rc;
    GetClientRect(app.hMainWnd, &rc);
    int W = rc.right;
    int H = rc.bottom;

    // Toolbar
    MoveWindow(g_hToolbar, 0, 0, W, TOOLBAR_H, TRUE);

    // Footer
    MoveWindow(g_hFooter, 0, H - FOOTER_H, W, FOOTER_H, TRUE);

    int editTop = TOOLBAR_H;
    int editH = H - TOOLBAR_H - FOOTER_H;

    bool isVert = (app.settings.layout == Layout::Vertical || app.settings.layout == Layout::VerticalReverse);
    bool isRev = (app.settings.layout == Layout::HorizontalReverse || app.settings.layout == Layout::VerticalReverse);

    HWND hFirstHeader = isRev ? g_hOutputHeader : g_hInputHeader;
    HWND hFirstEdit   = isRev ? app.hOutputEdit : app.hInputEdit;
    HWND hSecondHeader = isRev ? g_hInputHeader : g_hOutputHeader;
    HWND hSecondEdit  = isRev ? app.hInputEdit : app.hOutputEdit;

    if (isVert) {
        int halfH = (editH - DIVIDER_W) / 2;
        int pane1Top = editTop;
        int pane2Top = editTop + halfH + DIVIDER_W;

        // Pane headers
        MoveWindow(hFirstHeader, 0, pane1Top, W, PANE_HEADER_H, TRUE);
        MoveWindow(hFirstEdit, 0, pane1Top + PANE_HEADER_H, W, halfH - PANE_HEADER_H, TRUE);
        MoveWindow(g_hDivider, 0, pane1Top + halfH, W, DIVIDER_W, TRUE);
        MoveWindow(hSecondHeader, 0, pane2Top, W, PANE_HEADER_H, TRUE);
        MoveWindow(hSecondEdit, 0, pane2Top + PANE_HEADER_H, W, halfH - PANE_HEADER_H, TRUE);
    } else {
        int halfW = (W - DIVIDER_W) / 2;

        MoveWindow(hFirstHeader, 0, editTop, halfW, PANE_HEADER_H, TRUE);
        MoveWindow(hFirstEdit, 0, editTop + PANE_HEADER_H, halfW, editH - PANE_HEADER_H, TRUE);
        MoveWindow(g_hDivider, halfW, editTop, DIVIDER_W, editH, TRUE);
        MoveWindow(hSecondHeader, halfW + DIVIDER_W, editTop, W - halfW - DIVIDER_W, PANE_HEADER_H, TRUE);
        MoveWindow(hSecondEdit, halfW + DIVIDER_W, editTop + PANE_HEADER_H,
                   W - halfW - DIVIDER_W, editH - PANE_HEADER_H, TRUE);
    }

    // Toast position
    if (g_hToast) {
        MoveWindow(g_hToast, W / 2 - S(150), H - FOOTER_H - S(50), S(300), TOAST_H, TRUE);
    }

    InvalidateRect(app.hMainWnd, nullptr, TRUE);
}

// ── Toast ──
void ShowToast(HWND hWnd, const wchar_t* msg) {
    if (!g_hToast) return;
    SetWindowTextW(g_hToast, msg);
    ShowWindow(g_hToast, SW_SHOW);
    InvalidateRect(g_hToast, nullptr, TRUE);

    if (g_toastTimer) KillTimer(hWnd, g_toastTimer);
    g_toastTimer = SetTimer(hWnd, 9999, 2500, nullptr);
}

// ── Status ──
void SetStatus(AppState& app, StatusState state, const wchar_t* text) {
    app.status = state;
    if (g_hStatusText) SetWindowTextW(g_hStatusText, text);
    if (g_hStatusDot) InvalidateRect(g_hStatusDot, nullptr, TRUE);
}

// ── Stats ──
void UpdateStats(AppState& app) {
    int inLen = GetWindowTextLengthW(app.hInputEdit);
    int outLen = GetWindowTextLengthW(app.hOutputEdit);

    wchar_t buf[64];
    wsprintfW(buf, L"入力: %d文字", inLen);
    if (g_hInputStats) SetWindowTextW(g_hInputStats, buf);

    wsprintfW(buf, L"出力: %d文字", outLen);
    if (g_hOutputStats) SetWindowTextW(g_hOutputStats, buf);

    wsprintfW(buf, L"変換: %d回", app.conversionCount);
    if (g_hConvCount) SetWindowTextW(g_hConvCount, buf);

    wsprintfW(buf, L"%d字", outLen);
    if (g_hOutputCharCount) SetWindowTextW(g_hOutputCharCount, buf);
}

void UpdateStatusBar(AppState& app) {
    auto provStr = [](Provider p) -> const wchar_t* {
        switch (p) {
            case Provider::Google:    return L"Google";
            case Provider::Ollama:    return L"Ollama";
            default: return L"—";
        }
    };

    if (g_hProviderLabel) SetWindowTextW(g_hProviderLabel, provStr(app.settings.provider));
    if (g_hModelLabel) SetWindowTextW(g_hModelLabel, Utf8ToWide(app.settings.model).c_str());
}

// ── Conversion ──
void DoConvert(AppState& app) {
    if (app.isConverting) return;

    std::wstring input = GetEditText(app.hInputEdit);
    // Trim
    std::wstring trimmed = input;
    while (!trimmed.empty() && (trimmed.front() == L' ' || trimmed.front() == L'\t' ||
           trimmed.front() == L'\r' || trimmed.front() == L'\n')) trimmed.erase(0, 1);
    while (!trimmed.empty() && (trimmed.back() == L' ' || trimmed.back() == L'\t' ||
           trimmed.back() == L'\r' || trimmed.back() == L'\n')) trimmed.pop_back();
    if (trimmed.empty()) return;

    app.isConverting = true;
    SetStatus(app, StatusState::Converting, L"変換中...");

    std::string inputUtf8 = WideToUtf8(trimmed);
    std::wstring sentRaw = input;

    // Worker thread
    HWND hWnd = app.hMainWnd;
    std::thread([hWnd, inputUtf8, sentRaw]() {
        try {
            std::string result = CallLLM(*g_pApp, inputUtf8);
            // Post result to UI thread
            auto* pResult = new std::pair<std::string, std::wstring>(result, sentRaw);
            PostMessageW(hWnd, WM_LLM_COMPLETE, 0, (LPARAM)pResult);
        } catch (std::exception& e) {
            auto* pErr = new std::string(e.what());
            PostMessageW(hWnd, WM_LLM_ERROR, 0, (LPARAM)pErr);
        }
    }).detach();
}

// ── Custom paint for panels ──
static void PaintPaneHeader(HDC hdc, RECT rc, const wchar_t* title, bool isOutput, AppState& app) {
    HBRUSH hBr = CreateSolidBrush(app.theme.surface);
    FillRect(hdc, &rc, hBr);
    DeleteObject(hBr);

    // Bottom border
    HPEN hPen = CreatePen(PS_SOLID, 1, app.theme.border);
    HPEN hOld = (HPEN)SelectObject(hdc, hPen);
    MoveToEx(hdc, rc.left, rc.bottom - 1, nullptr);
    LineTo(hdc, rc.right, rc.bottom - 1);
    SelectObject(hdc, hOld);
    DeleteObject(hPen);

    // Title text
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, app.theme.textDimmer);
    SelectObject(hdc, app.hFontSmall);
    RECT tr = rc;
    tr.left += S(16);
    DrawTextW(hdc, title, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
}

// ── Status dot custom paint ──
static LRESULT CALLBACK StatusDotProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp,
                                       UINT_PTR id, DWORD_PTR data) {
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT rc;
        GetClientRect(hWnd, &rc);

        auto& app = *g_pApp;
        HBRUSH hBg = CreateSolidBrush(app.theme.surface2);
        FillRect(hdc, &rc, hBg);
        DeleteObject(hBg);

        COLORREF dotColor = app.theme.textDimmer;
        switch (app.status) {
            case StatusState::Converting: dotColor = app.theme.accent; break;
            case StatusState::Ok:         dotColor = app.theme.success; break;
            case StatusState::Error:      dotColor = app.theme.danger; break;
            default: break;
        }

        int cx = (rc.right - rc.left) / 2;
        int cy = (rc.bottom - rc.top) / 2;
        HBRUSH hDot = CreateSolidBrush(dotColor);
        HBRUSH hOld = (HBRUSH)SelectObject(hdc, hDot);
        HPEN hPen = CreatePen(PS_SOLID, 1, dotColor);
        HPEN hOldP = (HPEN)SelectObject(hdc, hPen);
        Ellipse(hdc, cx - S(3), cy - S(3), cx + S(4), cy + S(4));
        SelectObject(hdc, hOld);
        SelectObject(hdc, hOldP);
        DeleteObject(hDot);
        DeleteObject(hPen);

        EndPaint(hWnd, &ps);
        return 0;
    }
    return DefSubclassProc(hWnd, msg, wp, lp);
}

// ── Divider paint and drag ──
static LRESULT CALLBACK DividerProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp,
                                     UINT_PTR id, DWORD_PTR data) {
    auto& app = *g_pApp;
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);
            RECT rc;
            GetClientRect(hWnd, &rc);
            HBRUSH hBr = CreateSolidBrush(g_dragging ? app.theme.accent : app.theme.border);
            FillRect(hdc, &rc, hBr);
            DeleteObject(hBr);
            EndPaint(hWnd, &ps);
            return 0;
        }
        case WM_LBUTTONDOWN:
            g_dragging = true;
            SetCapture(hWnd);
            InvalidateRect(hWnd, nullptr, TRUE);
            return 0;
        case WM_MOUSEMOVE:
            if (g_dragging) {
                POINT pt;
                GetCursorPos(&pt);
                ScreenToClient(app.hMainWnd, &pt);
                RECT rc;
                GetClientRect(app.hMainWnd, &rc);
                bool isVert = (app.settings.layout == Layout::Vertical || app.settings.layout == Layout::VerticalReverse);

                if (isVert) {
                    int editTop = TOOLBAR_H;
                    int editH = rc.bottom - TOOLBAR_H - FOOTER_H;
                    int newH = pt.y - editTop;
                    newH = std::max(100, std::min(editH - 100, newH));

                    MoveWindow(g_hInputHeader, 0, editTop, rc.right, PANE_HEADER_H, TRUE);
                    MoveWindow(app.hInputEdit, 0, editTop + PANE_HEADER_H, rc.right, newH - PANE_HEADER_H, TRUE);
                    MoveWindow(g_hDivider, 0, editTop + newH, rc.right, DIVIDER_W, TRUE);
                    int remain = editH - newH - DIVIDER_W;
                    MoveWindow(g_hOutputHeader, 0, editTop + newH + DIVIDER_W, rc.right, PANE_HEADER_H, TRUE);
                    MoveWindow(app.hOutputEdit, 0, editTop + newH + DIVIDER_W + PANE_HEADER_H,
                               rc.right, remain - PANE_HEADER_H, TRUE);
                } else {
                    int newW = pt.x;
                    newW = std::max(200, std::min((int)rc.right - 200, newW));

                    int editTop = TOOLBAR_H;
                    int editH = rc.bottom - TOOLBAR_H - FOOTER_H;

                    MoveWindow(g_hInputHeader, 0, editTop, newW, PANE_HEADER_H, TRUE);
                    MoveWindow(app.hInputEdit, 0, editTop + PANE_HEADER_H, newW, editH - PANE_HEADER_H, TRUE);
                    MoveWindow(g_hDivider, newW, editTop, DIVIDER_W, editH, TRUE);
                    int rightW = rc.right - newW - DIVIDER_W;
                    MoveWindow(g_hOutputHeader, newW + DIVIDER_W, editTop, rightW, PANE_HEADER_H, TRUE);
                    MoveWindow(app.hOutputEdit, newW + DIVIDER_W, editTop + PANE_HEADER_H, rightW, editH - PANE_HEADER_H, TRUE);
                }
            }
            return 0;
        case WM_LBUTTONUP:
            if (g_dragging) {
                g_dragging = false;
                ReleaseCapture();
                InvalidateRect(hWnd, nullptr, TRUE);
            }
            return 0;
        case WM_SETCURSOR: {
            bool isVert = (app.settings.layout == Layout::Vertical || app.settings.layout == Layout::VerticalReverse);
            SetCursor(LoadCursor(nullptr, isVert ? IDC_SIZENS : IDC_SIZEWE));
            return TRUE;
        }
    }
    return DefSubclassProc(hWnd, msg, wp, lp);
}

// ── Toast paint ──
static LRESULT CALLBACK ToastProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp,
                                   UINT_PTR id, DWORD_PTR data) {
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT rc;
        GetClientRect(hWnd, &rc);

        auto& t = g_pApp->theme;
        HBRUSH hBr = CreateSolidBrush(t.surface2);
        FillRect(hdc, &rc, hBr);
        DeleteObject(hBr);

        HPEN hPen = CreatePen(PS_SOLID, 1, t.border);
        HPEN hOld = (HPEN)SelectObject(hdc, hPen);
        HBRUSH hNull = (HBRUSH)GetStockObject(NULL_BRUSH);
        SelectObject(hdc, hNull);
        RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, S(12), S(12));
        SelectObject(hdc, hOld);
        DeleteObject(hPen);

        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, t.text);
        SelectObject(hdc, g_pApp->hFontSmall);
        wchar_t text[256] = {};
        GetWindowTextW(hWnd, text, 256);
        DrawTextW(hdc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        EndPaint(hWnd, &ps);
        return 0;
    }
    return DefSubclassProc(hWnd, msg, wp, lp);
}

// ── Pane header paint ──
static LRESULT CALLBACK PaneHeaderProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp,
                                        UINT_PTR id, DWORD_PTR data) {
    if (msg == WM_COMMAND) {
        return SendMessageW(GetParent(hWnd), msg, wp, lp);
    }

    if (msg == WM_DRAWITEM) {
        auto* dis = (DRAWITEMSTRUCT*)lp;
        if (dis->CtlType == ODT_BUTTON) {
            PaintButton(dis);
            return TRUE;
        }
    }

    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT rc;
        GetClientRect(hWnd, &rc);

        bool isOutput = (GetDlgCtrlID(hWnd) == IDC_OUTPUT_HEADER);
        const wchar_t* title = isOutput ? L"出力 — 日本語" : L"入力 — ローマ字";
        PaintPaneHeader(hdc, rc, title, isOutput, *g_pApp);

        // Output char count on the right side
        if (isOutput && g_hOutputCharCount) {
            wchar_t buf[32] = {};
            GetWindowTextW(g_hOutputCharCount, buf, 32);
            RECT tr = rc;
            tr.right -= S(16);
            SetTextColor(hdc, g_pApp->theme.textDimmer);
            DrawTextW(hdc, buf, -1, &tr, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        }

        EndPaint(hWnd, &ps);
        return 0;
    }
    return DefSubclassProc(hWnd, msg, wp, lp);
}

// ── Toolbar paint ──
static LRESULT CALLBACK ToolbarProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp,
                                     UINT_PTR id, DWORD_PTR data) {
    if (msg == WM_COMMAND) {
        return SendMessageW(GetParent(hWnd), msg, wp, lp);
    }

    if (msg == WM_DRAWITEM) {
        auto* dis = (DRAWITEMSTRUCT*)lp;
        if (dis->CtlType == ODT_BUTTON) {
            PaintButton(dis);
            return TRUE;
        }
    }

    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT rc;
        GetClientRect(hWnd, &rc);

        auto& t = g_pApp->theme;
        HBRUSH hBr = CreateSolidBrush(t.surface);
        FillRect(hdc, &rc, hBr);
        DeleteObject(hBr);

        // Bottom border
        HPEN hPen = CreatePen(PS_SOLID, 1, t.border);
        HPEN hOld = (HPEN)SelectObject(hdc, hPen);
        MoveToEx(hdc, 0, rc.bottom - 1, nullptr);
        LineTo(hdc, rc.right, rc.bottom - 1);
        SelectObject(hdc, hOld);
        DeleteObject(hPen);

        // Logo
        SetBkMode(hdc, TRANSPARENT);
        SelectObject(hdc, g_pApp->hFontBold);
        RECT lr = {S(14), 0, S(120), rc.bottom};
        SetTextColor(hdc, t.accent);
        DrawTextW(hdc, L"Roma", -1, &lr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        // Measure "Roma" width
        SIZE sz;
        GetTextExtentPoint32W(hdc, L"Roma", 4, &sz);
        lr.left += sz.cx;
        SetTextColor(hdc, t.text);
        DrawTextW(hdc, L"jiTxted", -1, &lr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        EndPaint(hWnd, &ps);
        return 0;
    }
    return DefSubclassProc(hWnd, msg, wp, lp);
}

// ── Footer paint ──
static LRESULT CALLBACK FooterProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp,
                                    UINT_PTR id, DWORD_PTR data) {
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT rc;
        GetClientRect(hWnd, &rc);

        auto& t = g_pApp->theme;
        HBRUSH hBr = CreateSolidBrush(t.surface);
        FillRect(hdc, &rc, hBr);
        DeleteObject(hBr);

        // Top border
        HPEN hPen = CreatePen(PS_SOLID, 1, t.border);
        HPEN hOld = (HPEN)SelectObject(hdc, hPen);
        MoveToEx(hdc, 0, 0, nullptr);
        LineTo(hdc, rc.right, 0);
        SelectObject(hdc, hOld);
        DeleteObject(hPen);

        // Stats text
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, t.textDimmer);
        SelectObject(hdc, g_pApp->hFontSmall);

        // Left side: stats
        wchar_t buf[256] = {};
        wchar_t s1[64] = {}, s2[64] = {}, s3[64] = {};
        if (g_hInputStats) GetWindowTextW(g_hInputStats, s1, 64);
        if (g_hOutputStats) GetWindowTextW(g_hOutputStats, s2, 64);
        if (g_hConvCount) GetWindowTextW(g_hConvCount, s3, 64);
        wsprintfW(buf, L"  %s    %s    %s", s1, s2, s3);
        RECT lr = rc;
        lr.left += 8;
        DrawTextW(hdc, buf, -1, &lr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        // Right side: provider/model
        wchar_t p1[64] = {}, p2[128] = {};
        if (g_hProviderLabel) GetWindowTextW(g_hProviderLabel, p1, 64);
        if (g_hModelLabel) GetWindowTextW(g_hModelLabel, p2, 128);
        wsprintfW(buf, L"%s  %s  ", p1, p2);
        lr = rc;
        lr.right -= 8;
        DrawTextW(hdc, buf, -1, &lr, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

        EndPaint(hWnd, &ps);
        return 0;
    }
    return DefSubclassProc(hWnd, msg, wp, lp);
}

// ── Dialog WndProc: forward WM_COMMAND as posted WM_DLGCMD ──
static LRESULT CALLBACK DialogForwardProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_COMMAND:
        PostMessageW(hWnd, WM_DLGCMD, wp, lp);
        return 0;
    case WM_CLOSE:
        PostMessageW(hWnd, WM_DLGCMD, IDCANCEL, 0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wp, lp);
}

// ── Input Edit subclass (Enter to convert) ──
static LRESULT CALLBACK EditSubclassProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp,
                                          UINT_PTR id, DWORD_PTR data) {
    if (msg == WM_KEYDOWN && wp == VK_RETURN) {
        if (!(GetKeyState(VK_SHIFT) & 0x8000)) {
            DoConvert(*g_pApp);
            return 0;
        }
    }
    return DefSubclassProc(hWnd, msg, wp, lp);
}

// ── Init UI ──
bool InitUI(HINSTANCE hInstance, AppState& app) {
    g_pApp = &app;

    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_WIN95_CLASSES | ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

    // Register window class
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr; // Custom paint
    wc.lpszClassName = L"RomajiTxtedMain";
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    RegisterClassExW(&wc);

    WNDCLASSEXW dwc = {};
    dwc.cbSize = sizeof(dwc);
    dwc.lpfnWndProc = DialogForwardProc;
    dwc.hInstance = hInstance;
    dwc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    dwc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    dwc.lpszClassName = L"RomajiTxtedDialog";
    RegisterClassExW(&dwc);

    // Create main window
    app.hMainWnd = CreateWindowExW(
        0, L"RomajiTxtedMain",
        L"RomajiTxted — ローマ字→日本語エディタ",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 2000, 1400,
        nullptr, nullptr, hInstance, nullptr
    );
    if (!app.hMainWnd) return false;

    app.CreateGdiResources();

    // ── Toolbar (custom painted static) ──
    g_hToolbar = CreateWindowExW(0, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | SS_OWNERDRAW,
        0, 0, 1000, TOOLBAR_H, app.hMainWnd, nullptr, nullptr, nullptr);
    SetWindowSubclass(g_hToolbar, ToolbarProc, 0, 0);

    // Toolbar buttons (positioned right-aligned)
    int bx = 140; // Unscaled units after logo
    auto addBtn = [&](int id, const wchar_t* text, int w) -> HWND {
        HWND h = CreateThemeButton(g_hToolbar, id, text, bx, 8, w, 26);
        g_toolbarButtons.push_back(h);
        bx += w + 6;
        return h;
    };

    // Font size controls
    addBtn(IDC_BTN_FONT_DOWN, L"−", 26);
    g_hFontLabel = CreateWindowExW(0, L"STATIC", L"45px",
        WS_CHILD | WS_VISIBLE | SS_CENTER,
        S(bx), S(13), S(56), S(20), g_hToolbar, (HMENU)IDC_FONT_SIZE_LABEL, nullptr, nullptr);
    SendMessageW(g_hFontLabel, WM_SETFONT, (WPARAM)app.hFont, TRUE);
    bx += 60;
    addBtn(IDC_BTN_FONT_UP, L"＋", 26);
    bx += 10;

    addBtn(IDC_BTN_WRAP, L"折り返し", 60);
    addBtn(IDC_BTN_HISTORY, L"履歴", 42);
    addBtn(IDC_BTN_SETTINGS, L"⚙ 設定", 56);

    // Status badge (right side of toolbar)
    g_hStatusDot = CreateWindowExW(0, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE, 0, S(12), S(18), S(18), g_hToolbar,
        (HMENU)(INT_PTR)IDC_STATUS_DOT, nullptr, nullptr);
    SetWindowSubclass(g_hStatusDot, StatusDotProc, 0, 0);

    g_hStatusText = CreateWindowExW(0, L"STATIC", L"待機中",
        WS_CHILD | WS_VISIBLE, 0, S(13), S(90), S(20), g_hToolbar,
        (HMENU)(INT_PTR)IDC_STATUS_TEXT, nullptr, nullptr);
    SendMessageW(g_hStatusText, WM_SETFONT, (WPARAM)app.hFont, TRUE);

    // ── Pane Headers ──
    g_hInputHeader = CreateWindowExW(0, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | SS_OWNERDRAW,
        0, 0, 100, PANE_HEADER_H, app.hMainWnd,
        (HMENU)(INT_PTR)IDC_INPUT_HEADER, nullptr, nullptr);
    SetWindowSubclass(g_hInputHeader, PaneHeaderProc, 0, 0);

    g_hOutputHeader = CreateWindowExW(0, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | SS_OWNERDRAW,
        0, 0, 100, PANE_HEADER_H, app.hMainWnd,
        (HMENU)(INT_PTR)IDC_OUTPUT_HEADER, nullptr, nullptr);
    SetWindowSubclass(g_hOutputHeader, PaneHeaderProc, 0, 0);

    // Output char count (hidden storage)
    g_hOutputCharCount = CreateWindowExW(0, L"STATIC", L"0字",
        WS_CHILD, 0, 0, 0, 0, app.hMainWnd,
        (HMENU)(INT_PTR)IDC_OUTPUT_CHARCOUNT, nullptr, nullptr);

    // Output pane buttons on header
    int obx = 0; // Will be positioned in layout
    CreateThemeButton(g_hOutputHeader, IDC_BTN_COPY, L"📋コピー", 0, 3, 64, 26);
    CreateThemeButton(g_hOutputHeader, IDC_BTN_REMOVE_NL, L"⏎改行削除", 66, 3, 74, 26);
    CreateThemeButton(g_hOutputHeader, IDC_BTN_SAVE, L"💾保存", 142, 3, 52, 26);
    CreateThemeButton(g_hOutputHeader, IDC_BTN_EDIT_OUTPUT, L"✏編集", 196, 3, 52, 26);

    // Input pane buttons
    CreateThemeButton(g_hInputHeader, IDC_BTN_CONVERT, L"⚡変換", 0, 3, 52, 26);
    CreateThemeButton(g_hInputHeader, IDC_BTN_CLEAR, L"🗑", 54, 3, 30, 26);

    // ── Editors ──
    app.hInputEdit = CreateWindowExW(0, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL | ES_WANTRETURN,
        0, 0, 100, 100, app.hMainWnd,
        (HMENU)(INT_PTR)IDC_INPUT_EDIT, nullptr, nullptr);

    app.hOutputEdit = CreateWindowExW(0, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL | ES_READONLY,
        0, 0, 100, 100, app.hMainWnd,
        (HMENU)(INT_PTR)IDC_OUTPUT_EDIT, nullptr, nullptr);

    // Set edit fonts
    SendMessageW(app.hInputEdit, WM_SETFONT, (WPARAM)app.hFont, TRUE);
    SendMessageW(app.hOutputEdit, WM_SETFONT, (WPARAM)app.hFont, TRUE);

    // Subclass input for Enter handling
    SetWindowSubclass(app.hInputEdit, EditSubclassProc, 1, 0);

    // ── Divider ──
    g_hDivider = CreateWindowExW(0, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE,
        0, 0, DIVIDER_W, 100, app.hMainWnd,
        nullptr, nullptr, nullptr);
    SetWindowSubclass(g_hDivider, DividerProc, 0, 0);

    // ── Footer ──
    g_hFooter = CreateWindowExW(0, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | SS_OWNERDRAW,
        0, 0, 1000, FOOTER_H, app.hMainWnd,
        nullptr, nullptr, nullptr);
    SetWindowSubclass(g_hFooter, FooterProc, 0, 0);

    // Hidden stat labels
    g_hInputStats = CreateWindowExW(0, L"STATIC", L"入力: 0文字", WS_CHILD, 0,0,0,0, app.hMainWnd, nullptr, nullptr, nullptr);
    g_hOutputStats = CreateWindowExW(0, L"STATIC", L"出力: 0文字", WS_CHILD, 0,0,0,0, app.hMainWnd, nullptr, nullptr, nullptr);
    g_hConvCount = CreateWindowExW(0, L"STATIC", L"変換: 0回", WS_CHILD, 0,0,0,0, app.hMainWnd, nullptr, nullptr, nullptr);
    g_hProviderLabel = CreateWindowExW(0, L"STATIC", L"—", WS_CHILD, 0,0,0,0, app.hMainWnd, nullptr, nullptr, nullptr);
    g_hModelLabel = CreateWindowExW(0, L"STATIC", L"—", WS_CHILD, 0,0,0,0, app.hMainWnd, nullptr, nullptr, nullptr);

    // ── Toast ──
    g_hToast = CreateWindowExW(WS_EX_TOPMOST, L"STATIC", L"",
        WS_CHILD | SS_OWNERDRAW,
        0, 0, 300, TOAST_H, app.hMainWnd,
        nullptr, nullptr, nullptr);
    SetWindowSubclass(g_hToast, ToastProc, 0, 0);

    // Apply layout and show
    LayoutControls(app);
    UpdateStatusBar(app);
    UpdateStats(app);

    ShowWindow(app.hMainWnd, SW_SHOW);
    UpdateWindow(app.hMainWnd);

    return true;
}

// ── Position toolbar status badge on right side ──
static void PositionStatusBadge(int toolbarWidth) {
    if (g_hStatusDot && g_hStatusText) {
        MoveWindow(g_hStatusDot, toolbarWidth - S(130), S(14), S(18), S(18), TRUE);
        MoveWindow(g_hStatusText, toolbarWidth - S(108), S(8), S(100), S(26), TRUE);
    }
}

// ── Position output header buttons on right side ──
static void PositionOutputButtons() {
    RECT rc;
    GetClientRect(g_hOutputHeader, &rc);
    int right = rc.right;

    HWND hCopy = GetDlgItem(g_hOutputHeader, IDC_BTN_COPY);
    HWND hNL = GetDlgItem(g_hOutputHeader, IDC_BTN_REMOVE_NL);
    HWND hSave = GetDlgItem(g_hOutputHeader, IDC_BTN_SAVE);
    HWND hEdit = GetDlgItem(g_hOutputHeader, IDC_BTN_EDIT_OUTPUT);

    int x = right - S(4);
    if (hEdit) { x -= S(52); MoveWindow(hEdit, x, S(3), S(52), S(26), TRUE); x -= S(4); }
    if (hSave) { x -= S(52); MoveWindow(hSave, x, S(3), S(52), S(26), TRUE); x -= S(4); }
    if (hNL)   { x -= S(74); MoveWindow(hNL,   x, S(3), S(74), S(26), TRUE); x -= S(4); }
    if (hCopy) { x -= S(64); MoveWindow(hCopy, x, S(3), S(64), S(26), TRUE); }
}

// ── Position input header buttons on right side ──
static void PositionInputButtons() {
    RECT rc;
    GetClientRect(g_hInputHeader, &rc);
    int right = rc.right;

    HWND hConv = GetDlgItem(g_hInputHeader, IDC_BTN_CONVERT);
    HWND hClear = GetDlgItem(g_hInputHeader, IDC_BTN_CLEAR);

    int x = right - S(4);
    if (hClear) { x -= S(30); MoveWindow(hClear, x, S(3), S(30), S(26), TRUE); x -= S(4); }
    if (hConv) { x -= S(52); MoveWindow(hConv, x, S(3), S(52), S(26), TRUE); }
}

// ── Settings Dialog ──
static struct {
    AppState* pApp;
    std::wstring resultPassword;
} g_dlgData;

void ShowSettingsDialog(HWND hParent, AppState& app) {
    g_dlgData.pApp = &app;

    // Build dialog template in memory
    // This is complex with Win32 in-memory templates, so use CreateDialog approach
    // For simplicity, create a modeless child window styled as a dialog

    // We'll use a simple approach: create a popup window
    HWND hDlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        L"RomajiTxtedDialog", // Dialog class
        L"⚙ 設定",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME,
        0, 0, DS(540), DS(600),
        hParent, nullptr, nullptr, nullptr
    );

    // Center on parent
    RECT parentRc;
    GetWindowRect(hParent, &parentRc);
    int cx = (parentRc.left + parentRc.right) / 2 - DS(270);
    int cy = (parentRc.top + parentRc.bottom) / 2 - DS(300);
    SetWindowPos(hDlg, HWND_TOP, cx, cy, DS(540), DS(600), SWP_SHOWWINDOW);

    // Set dialog background
    // For simplicity, we'll use standard dialog appearance with custom colors

    auto addLabel = [&](const wchar_t* text, int y) {
        HWND h = CreateWindowExW(0, L"STATIC", text,
            WS_CHILD | WS_VISIBLE, DS(20), DS(y), DS(240), DS(20), hDlg, nullptr, nullptr, nullptr);
        SendMessageW(h, WM_SETFONT, (WPARAM)app.hFontSmall, TRUE);
        return h;
    };

    auto addCombo = [&](int id, int y, int h = 24) {
        HWND h2 = CreateWindowExW(0, L"COMBOBOX", L"",
            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
            DS(20), DS(y), DS(480), DS(200), hDlg, (HMENU)(INT_PTR)id, nullptr, nullptr);
        SendMessageW(h2, WM_SETFONT, (WPARAM)app.hFontSmall, TRUE);
        return h2;
    };

    auto addEdit = [&](int id, int y, DWORD style = 0, int h = 24) {
        HWND h2 = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | style,
            DS(20), DS(y), DS(480), DS(h), hDlg, (HMENU)(INT_PTR)id, nullptr, nullptr);
        SendMessageW(h2, WM_SETFONT, (WPARAM)app.hFontSmall, TRUE);
        return h2;
    };

    auto addMultiEdit = [&](int id, int y, int h) {
        HWND h2 = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL | ES_WANTRETURN,
            DS(20), DS(y), DS(480), DS(h), hDlg, (HMENU)(INT_PTR)id, nullptr, nullptr);
        SendMessageW(h2, WM_SETFONT, (WPARAM)app.hFontSmall, TRUE);
        return h2;
    };

    int y = 10;
    addLabel(L"LLM プロバイダー", y); y += 18;
    HWND hProv = addCombo(IDC_PROVIDER_COMBO, y); y += 32;

    // Combo index → Provider enum mapping (combo only has Google/Ollama)
    static const Provider kComboToProvider[] = { Provider::Google, Provider::Ollama };
    SendMessageW(hProv, CB_ADDSTRING, 0, (LPARAM)L"Google (Gemini)");
    SendMessageW(hProv, CB_ADDSTRING, 0, (LPARAM)L"Ollama (ローカル)");
    int provSelIdx = (app.settings.provider == Provider::Ollama) ? 1 : 0;
    SendMessageW(hProv, CB_SETCURSEL, provSelIdx, 0);

    addLabel(L"API キー", y); y += 18;
    HWND hKey = addEdit(IDC_APIKEY_EDIT, y, ES_PASSWORD); y += 32;
    if (app.settings.isEncrypted && !app.decryptedApiKey.empty()) {
        SetWindowTextW(hKey, Utf8ToWide(app.decryptedApiKey).c_str());
    } else if (!app.settings.isEncrypted && !app.settings.apiKey.empty()) {
        SetWindowTextW(hKey, Utf8ToWide(app.settings.apiKey).c_str());
    }

    addLabel(L"マスターパスワード", y); y += 18;
    addEdit(IDC_MASTER_PW_EDIT, y, ES_PASSWORD); y += 32;

    addLabel(L"モデル", y); y += 18;
    HWND hModelCombo = CreateWindowExW(0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWN | WS_VSCROLL,
        DS(20), DS(y), DS(400), DS(200), hDlg, (HMENU)(INT_PTR)IDC_MODEL_CUSTOM, nullptr, nullptr);
    SendMessageW(hModelCombo, WM_SETFONT, (WPARAM)app.hFontSmall, TRUE);
    SetWindowTextW(hModelCombo, Utf8ToWide(app.settings.model).c_str());
    HWND hFetchBtn = CreateWindowExW(0, L"BUTTON", L"取得",
        WS_CHILD | WS_VISIBLE,
        DS(424), DS(y), DS(80), DS(24), hDlg, (HMENU)(INT_PTR)IDC_BTN_FETCH_MODELS, nullptr, nullptr);
    SendMessageW(hFetchBtn, WM_SETFONT, (WPARAM)app.hFontSmall, TRUE);
    y += 32;

    addLabel(L"Ollama エンドポイント", y); y += 18;
    HWND hOllama = addEdit(IDC_OLLAMA_URL, y); y += 32;
    SetWindowTextW(hOllama, Utf8ToWide(app.settings.ollamaUrl).c_str());

    addLabel(L"ユーザー辞書", y); y += 18;
    HWND hDict = addMultiEdit(IDC_USER_DICT, y, 60); y += 68;
    SetWindowTextW(hDict, app.settings.userDictionary.c_str());

    addLabel(L"システムプロンプト", y); y += 18;
    HWND hPrompt = addMultiEdit(IDC_SYSTEM_PROMPT, y, 120); y += 128;
    SetWindowTextW(hPrompt, app.settings.systemPrompt.c_str());

    addLabel(L"レイアウト", y); y += 18;
    HWND hLayout = addCombo(IDC_LAYOUT_COMBO, y); y += 32;
    SendMessageW(hLayout, CB_ADDSTRING, 0, (LPARAM)L"左右（左に入力、右に出力）");
    SendMessageW(hLayout, CB_ADDSTRING, 0, (LPARAM)L"左右反転");
    SendMessageW(hLayout, CB_ADDSTRING, 0, (LPARAM)L"上下（上に入力、下に出力）");
    SendMessageW(hLayout, CB_ADDSTRING, 0, (LPARAM)L"上下反転");
    SendMessageW(hLayout, CB_SETCURSEL, (int)app.settings.layout, 0);

    // Buttons
    HWND hSave = CreateWindowExW(0, L"BUTTON", L"保存",
        WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
        DS(340), DS(y + 8), DS(80), DS(30), hDlg, (HMENU)(INT_PTR)IDC_BTN_SAVE_SETTINGS, nullptr, nullptr);
    HWND hCancel = CreateWindowExW(0, L"BUTTON", L"キャンセル",
        WS_CHILD | WS_VISIBLE,
        DS(424), DS(y + 8), DS(80), DS(30), hDlg, (HMENU)(INT_PTR)IDC_BTN_CANCEL_SETTINGS, nullptr, nullptr);
    SendMessageW(hSave, WM_SETFONT, (WPARAM)app.hFontSmall, TRUE);
    SendMessageW(hCancel, WM_SETFONT, (WPARAM)app.hFontSmall, TRUE);

    // Resize dialog to fit
    SetWindowPos(hDlg, HWND_TOP, cx, cy, DS(540), DS(y + 80), SWP_NOMOVE);

    // Modal message loop
    EnableWindow(hParent, FALSE);
    MSG msg;
    bool running = true;
    while (running && GetMessageW(&msg, nullptr, 0, 0)) {
        if (msg.message == WM_DLGCMD) {
            int cmd = LOWORD(msg.wParam);
            if (cmd == IDC_BTN_SAVE_SETTINGS) {
                // Save settings
                int provIdx = (int)SendMessageW(GetDlgItem(hDlg, IDC_PROVIDER_COMBO), CB_GETCURSEL, 0, 0);
                app.settings.provider = kComboToProvider[provIdx];

                std::wstring apiKeyW = GetEditText(GetDlgItem(hDlg, IDC_APIKEY_EDIT));
                std::wstring masterPwW = GetEditText(GetDlgItem(hDlg, IDC_MASTER_PW_EDIT));
                std::string apiKey = WideToUtf8(apiKeyW);
                std::string masterPw = WideToUtf8(masterPwW);

                if (app.settings.provider != Provider::Ollama && !apiKey.empty()) {
                    if (!masterPw.empty()) {
                        std::string encrypted = EncryptApiKey(apiKey, masterPw);
                        if (!encrypted.empty()) {
                            app.settings.apiKey = encrypted;
                            app.settings.isEncrypted = true;
                            app.decryptedApiKey = apiKey;
                        }
                    } else if (app.settings.isEncrypted && apiKey == app.decryptedApiKey) {
                        // No change to encryption
                    } else {
                        app.settings.apiKey = apiKey;
                        app.settings.isEncrypted = false;
                        app.decryptedApiKey = apiKey;
                    }
                } else if (app.settings.provider == Provider::Ollama) {
                    app.settings.apiKey = "";
                    app.settings.isEncrypted = false;
                }

                app.settings.model = WideToUtf8(GetEditText(GetDlgItem(hDlg, IDC_MODEL_CUSTOM)));
                app.settings.ollamaUrl = WideToUtf8(GetEditText(GetDlgItem(hDlg, IDC_OLLAMA_URL)));
                app.settings.userDictionary = GetEditText(GetDlgItem(hDlg, IDC_USER_DICT));
                app.settings.systemPrompt = GetEditText(GetDlgItem(hDlg, IDC_SYSTEM_PROMPT));
                app.settings.systemPromptVersion = 2;

                int layoutIdx = (int)SendMessageW(GetDlgItem(hDlg, IDC_LAYOUT_COMBO), CB_GETCURSEL, 0, 0);
                app.settings.layout = (Layout)layoutIdx;

                SaveSettings(app.settings);
                LayoutControls(app);
                UpdateStatusBar(app);
                ShowToast(app.hMainWnd, L"設定を保存しました");
                running = false;
            } else if (cmd == IDC_BTN_FETCH_MODELS) {
                // Fetch model list
                int provIdx = (int)SendMessageW(GetDlgItem(hDlg, IDC_PROVIDER_COMBO), CB_GETCURSEL, 0, 0);
                Provider prov = kComboToProvider[provIdx];
                std::wstring apiKeyW = GetEditText(GetDlgItem(hDlg, IDC_APIKEY_EDIT));
                std::string fetchApiKey = WideToUtf8(apiKeyW);
                std::string fetchOllamaUrl = WideToUtf8(GetEditText(GetDlgItem(hDlg, IDC_OLLAMA_URL)));
                if (fetchApiKey.empty() && !app.decryptedApiKey.empty()) fetchApiKey = app.decryptedApiKey;

                HWND hCombo = GetDlgItem(hDlg, IDC_MODEL_CUSTOM);
                // Save current text
                std::wstring curModel = GetEditText(hCombo);
                auto models = FetchModelList(prov, fetchApiKey, fetchOllamaUrl);
                SendMessageW(hCombo, CB_RESETCONTENT, 0, 0);
                int selIdx = -1;
                for (int i = 0; i < (int)models.size(); i++) {
                    std::wstring label = Utf8ToWide(models[i].label + " (" + models[i].id + ")");
                    SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)Utf8ToWide(models[i].id).c_str());
                    if (Utf8ToWide(models[i].id) == curModel) selIdx = i;
                }
                if (selIdx >= 0) {
                    SendMessageW(hCombo, CB_SETCURSEL, selIdx, 0);
                } else {
                    SetWindowTextW(hCombo, curModel.c_str());
                }
                if (models.empty()) {
                    // Notify user
                }
            } else if (cmd == IDC_BTN_CANCEL_SETTINGS || cmd == IDCANCEL) {
                running = false;
            }
            continue;
        }
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) {
            running = false;
            continue;
        }
        if (!IsDialogMessageW(hDlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    EnableWindow(hParent, TRUE);
    SetForegroundWindow(hParent);
    DestroyWindow(hDlg);
}

// ── Password Dialog ──
std::wstring ShowPasswordDialog(HWND hParent) {
    // Simple input box
    HWND hDlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        L"RomajiTxtedDialog", L"🔑 マスターパスワード入力",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME,
        0, 0, DS(540), DS(240),
        hParent, nullptr, nullptr, nullptr
    );

    RECT parentRc;
    GetWindowRect(hParent, &parentRc);
    int cx = (parentRc.left + parentRc.right) / 2 - DS(270);
    int cy = (parentRc.top + parentRc.bottom) / 2 - DS(120);
    SetWindowPos(hDlg, HWND_TOP, cx, cy, DS(540), DS(240), SWP_SHOWWINDOW);

    HWND hLabel = CreateWindowExW(0, L"STATIC",
        L"暗号化されたAPIキーを復号するために\nパスワードを入力してください。",
        WS_CHILD | WS_VISIBLE, DS(20), DS(20), DS(500), DS(80), hDlg, nullptr, nullptr, nullptr);
    SendMessageW(hLabel, WM_SETFONT, (WPARAM)g_pApp->hFont, TRUE);

    HWND hEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_PASSWORD | ES_AUTOHSCROLL,
        DS(20), DS(110), DS(500), DS(40), hDlg, (HMENU)(INT_PTR)IDC_DECRYPT_PW_EDIT, nullptr, nullptr);
    SendMessageW(hEdit, WM_SETFONT, (WPARAM)g_pApp->hFont, TRUE);
    SetFocus(hEdit);

    HWND hBtn = CreateWindowExW(0, L"BUTTON", L"OK",
        WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
        DS(380), DS(170), DS(140), DS(44), hDlg, (HMENU)(INT_PTR)IDC_BTN_PW_OK, nullptr, nullptr);
    SendMessageW(hBtn, WM_SETFONT, (WPARAM)g_pApp->hFont, TRUE);

    std::wstring result;

    EnableWindow(hParent, FALSE);
    MSG msg;
    bool running = true;
    while (running && GetMessageW(&msg, nullptr, 0, 0)) {
        if (msg.message == WM_DLGCMD) {
            int cmd = LOWORD(msg.wParam);
            if (cmd == IDC_BTN_PW_OK) {
                result = GetEditText(hEdit);
                running = false;
                continue;
            }
            if (cmd == IDCANCEL) {
                running = false;
                continue;
            }
        }
        if (msg.message == WM_KEYDOWN) {
            if (msg.wParam == VK_RETURN) {
                result = GetEditText(hEdit);
                running = false;
                continue;
            }
            if (msg.wParam == VK_ESCAPE) {
                running = false;
                continue;
            }
        }
        if (!IsDialogMessageW(hDlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    EnableWindow(hParent, TRUE);
    SetForegroundWindow(hParent);
    DestroyWindow(hDlg);
    return result;
}

void ApplyLayout(AppState& app) {
    LayoutControls(app);
}

// ── Main Window Proc ──
static LRESULT CALLBACK MainWndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto& app = *g_pApp;

    switch (msg) {
    case WM_SIZE:
        LayoutControls(app);
        PositionStatusBadge(LOWORD(lp));
        PositionOutputButtons();
        PositionInputButtons();
        return 0;

    case WM_ERASEBKGND: {
        HDC hdc = (HDC)wp;
        RECT rc;
        GetClientRect(hWnd, &rc);
        FillRect(hdc, &rc, app.hBgBrush);
        return 1;
    }

    case WM_CTLCOLOREDIT: {
        HDC hdc = (HDC)wp;
        SetTextColor(hdc, app.theme.text);
        SetBkColor(hdc, app.theme.bg);
        return (LRESULT)app.hBgBrush;
    }

    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wp;
        HWND hCtl = (HWND)lp;
        if (hCtl == g_hFontLabel || hCtl == g_hStatusText) {
            SetTextColor(hdc, app.theme.textDim);
            SetBkMode(hdc, TRANSPARENT);
            return (LRESULT)app.hSurface2Brush;
        }
        SetTextColor(hdc, app.theme.textDim);
        SetBkColor(hdc, app.theme.surface);
        return (LRESULT)app.hSurfaceBrush;
    }

    case WM_DRAWITEM: {
        auto* dis = (DRAWITEMSTRUCT*)lp;
        if (dis->CtlType == ODT_BUTTON) {
            PaintButton(dis);
            return TRUE;
        }
        return 0;
    }

    case WM_COMMAND: {
        int id = LOWORD(wp);
        int code = HIWORD(wp);

        switch (id) {
        case IDC_BTN_CONVERT:
            DoConvert(app);
            break;

        case IDC_BTN_CLEAR:
            SetWindowTextW(app.hInputEdit, L"");
            UpdateStats(app);
            break;

        case IDC_BTN_COPY: {
            std::wstring text = GetEditText(app.hOutputEdit);
            if (text.empty()) { ShowToast(hWnd, L"コピーするテキストがありません"); break; }
            if (OpenClipboard(hWnd)) {
                EmptyClipboard();
                HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, (text.size() + 1) * sizeof(wchar_t));
                if (hMem) {
                    memcpy(GlobalLock(hMem), text.c_str(), (text.size() + 1) * sizeof(wchar_t));
                    GlobalUnlock(hMem);
                    SetClipboardData(CF_UNICODETEXT, hMem);
                }
                CloseClipboard();
                ShowToast(hWnd, L"クリップボードにコピーしました");
            }
            break;
        }

        case IDC_BTN_REMOVE_NL: {
            std::wstring text = GetEditText(app.hOutputEdit);
            if (text.empty()) { ShowToast(hWnd, L"テキストがありません"); break; }
            std::wstring clean;
            for (wchar_t c : text) {
                if (c != L'\r' && c != L'\n') clean += c;
            }
            SetWindowTextW(app.hOutputEdit, clean.c_str());
            UpdateStats(app);
            ShowToast(hWnd, L"改行を削除しました");
            break;
        }

        case IDC_BTN_SAVE: {
            std::wstring text = GetEditText(app.hOutputEdit);
            if (text.empty()) { ShowToast(hWnd, L"保存するテキストがありません"); break; }

            SYSTEMTIME st;
            GetLocalTime(&st);
            wchar_t defName[128];
            wsprintfW(defName, L"romaji_output_%04d-%02d-%02d_%02d-%02d.txt",
                      st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);

            OPENFILENAMEW ofn = {};
            wchar_t filename[MAX_PATH] = {};
            wcscpy_s(filename, defName);
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = hWnd;
            ofn.lpstrFilter = L"テキストファイル (*.txt)\0*.txt\0すべてのファイル (*.*)\0*.*\0";
            ofn.lpstrFile = filename;
            ofn.nMaxFile = MAX_PATH;
            ofn.Flags = OFN_OVERWRITEPROMPT;
            ofn.lpstrDefExt = L"txt";

            if (GetSaveFileNameW(&ofn)) {
                std::string utf8 = WideToUtf8(text);
                HANDLE hFile = CreateFileW(filename, GENERIC_WRITE, 0, nullptr,
                    CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (hFile != INVALID_HANDLE_VALUE) {
                    // Write UTF-8 BOM
                    DWORD written;
                    unsigned char bom[] = {0xEF, 0xBB, 0xBF};
                    WriteFile(hFile, bom, 3, &written, nullptr);
                    WriteFile(hFile, utf8.c_str(), (DWORD)utf8.size(), &written, nullptr);
                    CloseHandle(hFile);
                    ShowToast(hWnd, L"ファイルを保存しました");
                } else {
                    ShowToast(hWnd, L"保存に失敗しました");
                }
            }
            break;
        }

        case IDC_BTN_EDIT_OUTPUT:
            app.outputEditable = !app.outputEditable;
            SendMessageW(app.hOutputEdit, EM_SETREADONLY, !app.outputEditable, 0);
            SetWindowTextW(GetDlgItem(g_hOutputHeader, IDC_BTN_EDIT_OUTPUT),
                          app.outputEditable ? L"🔒固定" : L"✏編集");
            InvalidateRect(GetDlgItem(g_hOutputHeader, IDC_BTN_EDIT_OUTPUT), nullptr, TRUE);
            break;

        case IDC_BTN_SETTINGS:
            ShowSettingsDialog(hWnd, app);
            break;

        case IDC_BTN_WRAP:
            app.wordWrap = !app.wordWrap;
            // Recreate edit controls to change wrap behavior
            // ES_AUTOHSCROLL controls word wrap in multiline edits
            {
                std::wstring inText = GetEditText(app.hInputEdit);
                std::wstring outText = GetEditText(app.hOutputEdit);

                DWORD baseStyle = WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL | ES_WANTRETURN;
                if (!app.wordWrap) baseStyle |= ES_AUTOHSCROLL | WS_HSCROLL;

                DestroyWindow(app.hInputEdit);
                DestroyWindow(app.hOutputEdit);

                app.hInputEdit = CreateWindowExW(0, L"EDIT", L"",
                    baseStyle, 0, 0, 100, 100, hWnd,
                    (HMENU)(INT_PTR)IDC_INPUT_EDIT, nullptr, nullptr);
                app.hOutputEdit = CreateWindowExW(0, L"EDIT", L"",
                    baseStyle | (app.outputEditable ? 0 : ES_READONLY),
                    0, 0, 100, 100, hWnd,
                    (HMENU)(INT_PTR)IDC_OUTPUT_EDIT, nullptr, nullptr);

                SendMessageW(app.hInputEdit, WM_SETFONT, (WPARAM)app.hFont, TRUE);
                SendMessageW(app.hOutputEdit, WM_SETFONT, (WPARAM)app.hFont, TRUE);
                SetWindowSubclass(app.hInputEdit, EditSubclassProc, 1, 0);

                SetWindowTextW(app.hInputEdit, inText.c_str());
                SetWindowTextW(app.hOutputEdit, outText.c_str());

                LayoutControls(app);
            }
            InvalidateRect(GetDlgItem(g_hToolbar, IDC_BTN_WRAP), nullptr, TRUE);
            break;

        case IDC_BTN_FONT_DOWN:
            if (app.fontSize > 30) {
                app.fontSize--;
                app.UpdateFonts();
                SendMessageW(app.hInputEdit, WM_SETFONT, (WPARAM)app.hFont, TRUE);
                SendMessageW(app.hOutputEdit, WM_SETFONT, (WPARAM)app.hFont, TRUE);
                wchar_t buf[16];
                wsprintfW(buf, L"%dpx", app.fontSize);
                SetWindowTextW(g_hFontLabel, buf);
            }
            break;

        case IDC_BTN_FONT_UP:
            if (app.fontSize < 84) {
                app.fontSize++;
                app.UpdateFonts();
                SendMessageW(app.hInputEdit, WM_SETFONT, (WPARAM)app.hFont, TRUE);
                SendMessageW(app.hOutputEdit, WM_SETFONT, (WPARAM)app.hFont, TRUE);
                wchar_t buf[16];
                wsprintfW(buf, L"%dpx", app.fontSize);
                SetWindowTextW(g_hFontLabel, buf);
            }
            break;

        case IDC_BTN_HISTORY:
            ShowToast(hWnd, L"履歴機能は今後実装予定です");
            break;

        case IDC_INPUT_EDIT:
        case IDC_OUTPUT_EDIT:
            if (code == EN_CHANGE) UpdateStats(app);
            break;
        }
        return 0;
    }

    case WM_LLM_COMPLETE: {
        auto* pResult = (std::pair<std::string, std::wstring>*)lp;
        std::wstring result = Utf8ToWide(pResult->first);
        std::wstring sentRaw = pResult->second;
        delete pResult;

        // Append to output
        std::wstring existing = GetEditText(app.hOutputEdit);
        if (!existing.empty() && !existing.empty() && existing.back() != L'\n') {
            existing += L"\r\n";
        }
        existing += result;
        SetEditText(app.hOutputEdit, existing);
        SendMessageW(app.hOutputEdit, EM_SETSEL, existing.size(), existing.size());
        SendMessageW(app.hOutputEdit, EM_SCROLLCARET, 0, 0);

        // Add history
        std::wstring trimmedInput = sentRaw;
        while (!trimmedInput.empty() && (trimmedInput.front() == L' ' || trimmedInput.front() == L'\r' || trimmedInput.front() == L'\n')) trimmedInput.erase(0, 1);
        while (!trimmedInput.empty() && (trimmedInput.back() == L' ' || trimmedInput.back() == L'\r' || trimmedInput.back() == L'\n')) trimmedInput.pop_back();
        app.AddHistory(trimmedInput, result);

        // Clear input (keep text added during conversion)
        std::wstring current = GetEditText(app.hInputEdit);
        if (current.length() >= sentRaw.length() && current.substr(0, sentRaw.length()) == sentRaw) {
            std::wstring remaining = current.substr(sentRaw.length());
            while (!remaining.empty() && remaining.front() == L'\n') remaining.erase(0, 1);
            SetEditText(app.hInputEdit, remaining);
        } else {
            SetEditText(app.hInputEdit, L"");
        }

        app.conversionCount++;
        app.isConverting = false;
        SetStatus(app, StatusState::Ok, L"変換完了");
        UpdateStats(app);

        SetTimer(hWnd, 8888, 3000, nullptr);
        return 0;
    }

    case WM_LLM_ERROR: {
        auto* pErr = (std::string*)lp;
        std::wstring msg = L"エラー: " + Utf8ToWide(pErr->substr(0, 60));
        delete pErr;

        app.isConverting = false;
        SetStatus(app, StatusState::Error, msg.c_str());
        ShowToast(hWnd, msg.c_str());

        SetTimer(hWnd, 8888, 3000, nullptr);
        return 0;
    }

    case WM_TIMER:
        if (wp == 9999) {
            // Toast hide
            ShowWindow(g_hToast, SW_HIDE);
            KillTimer(hWnd, 9999);
            g_toastTimer = 0;
        }
        if (wp == 8888) {
            // Reset status after conversion
            if (!app.isConverting) {
                SetStatus(app, StatusState::Idle, L"待機中");
            }
            KillTimer(hWnd, 8888);
        }
        return 0;

    case WM_KEYDOWN:
        // Ctrl+, for settings
        if ((GetKeyState(VK_CONTROL) & 0x8000) && wp == VK_OEM_COMMA) {
            ShowSettingsDialog(hWnd, app);
            return 0;
        }
        if (wp == VK_ESCAPE) {
            return 0;
        }
        break;

    case WM_GETMINMAXINFO: {
        auto* mmi = (MINMAXINFO*)lp;
        mmi->ptMinTrackSize.x = 600;
        mmi->ptMinTrackSize.y = 400;
        return 0;
    }

    case WM_DESTROY:
        app.CleanupGdi();
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hWnd, msg, wp, lp);
}
