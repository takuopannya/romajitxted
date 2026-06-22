#include <windows.h>
#include "app.h"
#include "ui.h"
#include "settings.h"
#include "crypto.h"

extern AppState g_app;

static std::string WideToUtf8(const std::wstring& w);

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int) {
    // Set process DPI awareness
    SetProcessDPIAware();

    // Load settings
    LoadSettings(g_app.settings);

    // If system prompt is empty, set default
    if (g_app.settings.systemPrompt.empty()) {
        g_app.settings.systemPrompt = GetDefaultSystemPrompt();
    }

    // Initialize UI
    if (!InitUI(hInstance, g_app)) {
        MessageBoxW(nullptr, L"UI初期化に失敗しました", L"エラー", MB_ICONERROR);
        return 1;
    }

    // If encrypted API key exists, prompt for password
    if (g_app.settings.isEncrypted && !g_app.settings.apiKey.empty()
        && g_app.settings.provider != Provider::Ollama) {
        while (g_app.decryptedApiKey.empty()) {
            std::wstring pw = ShowPasswordDialog(g_app.hMainWnd);
            if (pw.empty()) break; // User cancelled

            std::string result = DecryptApiKey(g_app.settings.apiKey, WideToUtf8(pw));
            if (!result.empty()) {
                g_app.decryptedApiKey = result;
            } else {
                ShowToast(g_app.hMainWnd, L"パスワードが違います。再入力してください。");
            }
        }
    } else if (!g_app.settings.isEncrypted) {
        g_app.decryptedApiKey = g_app.settings.apiKey;
    }

    // If no provider configured, open settings
    if (g_app.settings.model.empty()) {
        ShowSettingsDialog(g_app.hMainWnd, g_app);
    }

    // Focus input
    SetFocus(g_app.hInputEdit);

    // Message loop
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return (int)msg.wParam;
}

// WideToUtf8 used in main
static std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(len, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], len, nullptr, nullptr);
    return s;
}
