#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <functional>

// ── Theme Colors (light theme) ──
struct ThemeColors {
    COLORREF bg         = RGB(255, 255, 255); // #ffffff
    COLORREF surface    = RGB(249, 250, 251); // #f9fafb
    COLORREF surface2   = RGB(243, 244, 246); // #f3f4f6
    COLORREF border     = RGB(209, 213, 219); // #d1d5db
    COLORREF accent     = RGB(37, 99, 235);   // #2563eb
    COLORREF accent2    = RGB(14, 165, 233);  // #0ea5e9
    COLORREF text       = RGB(17, 24, 39);    // #111827
    COLORREF textDim    = RGB(75, 85, 99);    // #4b5563
    COLORREF textDimmer = RGB(107, 114, 128); // #6b7280
    COLORREF success    = RGB(22, 163, 74);   // #16a34a
    COLORREF warning    = RGB(202, 138, 4);   // #ca8a04
    COLORREF danger     = RGB(220, 38, 38);   // #dc2626
};

// ── History Item ──
struct HistoryItem {
    std::wstring romaji;
    std::wstring japanese;
    std::wstring time;
};

// ── LLM Provider ──
enum class Provider {
    OpenAI,
    Anthropic,
    Google,
    Ollama
};

// ── Model Info ──
struct ModelInfo {
    std::string id;
    std::string label;
};

// ── Layout ──
enum class Layout {
    Horizontal,
    HorizontalReverse,
    Vertical,
    VerticalReverse
};

// ── Status ──
enum class StatusState {
    Idle,
    Converting,
    Ok,
    Error
};

// ── App Settings ──
struct AppSettings {
    Provider provider = Provider::OpenAI;
    std::string model = "gpt-4o-mini";
    std::string apiKey;          // encrypted JSON or plain
    bool isEncrypted = false;
    std::string ollamaUrl = "http://localhost:11434";
    Layout layout = Layout::Horizontal;
    std::wstring userDictionary;
    std::wstring systemPrompt;
    int systemPromptVersion = 0;
};

// ── App State ──
struct AppState {
    // Settings
    AppSettings settings;
    std::string decryptedApiKey;

    // UI state
    int fontSize = 45;
    bool wordWrap = true;
    bool outputEditable = false;
    bool historyOpen = false;
    bool isConverting = false;
    int conversionCount = 0;
    StatusState status = StatusState::Idle;

    // History
    std::vector<HistoryItem> history;

    // Theme
    ThemeColors theme;

    // Window handles (set by UI)
    HWND hMainWnd = nullptr;
    HWND hInputEdit = nullptr;
    HWND hOutputEdit = nullptr;
    HWND hStatusBar = nullptr;

    // GDI resources
    HBRUSH hBgBrush = nullptr;
    HBRUSH hSurfaceBrush = nullptr;
    HBRUSH hSurface2Brush = nullptr;
    HBRUSH hBorderBrush = nullptr;
    HBRUSH hAccentBrush = nullptr;
    HFONT hFont = nullptr;
    HFONT hFontSmall = nullptr;
    HFONT hFontBold = nullptr;
    HFONT hFontMono = nullptr;

    void CreateGdiResources() {
        hBgBrush = CreateSolidBrush(theme.bg);
        hSurfaceBrush = CreateSolidBrush(theme.surface);
        hSurface2Brush = CreateSolidBrush(theme.surface2);
        hBorderBrush = CreateSolidBrush(theme.border);
        hAccentBrush = CreateSolidBrush(theme.accent);
        UpdateFonts();
    }

    void UpdateFonts() {
        if (hFont) DeleteObject(hFont);
        if (hFontSmall) DeleteObject(hFontSmall);
        if (hFontBold) DeleteObject(hFontBold);
        if (hFontMono) DeleteObject(hFontMono);

        hFont = CreateFontW(-fontSize, 0, 0, 0, FW_NORMAL, 0, 0, 0,
            DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Noto Sans JP");
        hFontSmall = CreateFontW(-36, 0, 0, 0, FW_NORMAL, 0, 0, 0,
            DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Yu Gothic UI");
        hFontBold = CreateFontW(-42, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0,
            DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Yu Gothic UI");
        hFontMono = CreateFontW(-39, 0, 0, 0, FW_NORMAL, 0, 0, 0,
            DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Consolas");
    }

    void CleanupGdi() {
        auto del = [](HGDIOBJ& h) { if (h) { DeleteObject(h); h = nullptr; } };
        del((HGDIOBJ&)hBgBrush);
        del((HGDIOBJ&)hSurfaceBrush);
        del((HGDIOBJ&)hSurface2Brush);
        del((HGDIOBJ&)hBorderBrush);
        del((HGDIOBJ&)hAccentBrush);
        del((HGDIOBJ&)hFont);
        del((HGDIOBJ&)hFontSmall);
        del((HGDIOBJ&)hFontBold);
        del((HGDIOBJ&)hFontMono);
    }

    void AddHistory(const std::wstring& romaji, const std::wstring& japanese) {
        HistoryItem item;
        item.romaji = romaji;
        item.japanese = japanese;

        SYSTEMTIME st;
        GetLocalTime(&st);
        wchar_t buf[32];
        wsprintfW(buf, L"%02d:%02d", st.wHour, st.wMinute);
        item.time = buf;

        history.insert(history.begin(), item);
        if (history.size() > 100) history.pop_back();
    }
};

// ── Default System Prompt ──
inline const wchar_t* GetDefaultSystemPrompt() {
    return
        L"あなたはローマ字入力を日本語テキストに変換する「入力補助装置」です。IMEの代替として機能します。\r\n"
        L"\r\n"
        L"【目的】\r\n"
        L"ユーザーはローマ字で入力することで、思考の流れを妨げずに日本語テキストを作成しています。あなたの役割は、ローマ字を正確に日本語へ復元することだけです。\r\n"
        L"\r\n"
        L"【役割の定義】\r\n"
        L"あなたは「文章を作る存在」ではなく「ユーザーが打った文字列を正しい日本語に復元する装置」です。ユーザーの入力がすべてであり、あなたが内容を判断・改変する権限はありません。\r\n"
        L"\r\n"
        L"【変換ルール】\r\n"
        L"1. ローマ字を対応する日本語（漢字・ひらがな・カタカナ混じり文）に変換する\r\n"
        L"2. 打ち間違い（タイポ）は文脈から最も近い意図を推測し修正する\r\n"
        L"   - 隣接キーの誤打（例: tomodavhi → tomodachi → 友達）\r\n"
        L"   - 文字の欠落（例: ohyougozaimsu → ohayougozaimasu → おはようございます）\r\n"
        L"   - 文字の入れ替わり（例: sakuan → sakana → 魚）\r\n"
        L"   - 余分な文字の混入（例: waratashi → watashi → 私）\r\n"
        L"3. 変換後の日本語のみを出力する。説明・注釈・補足は一切付けない\r\n"
        L"4. 入力にある句読点や改行はそのまま保持する。ユーザーが打っていない句読点を追加しない\r\n"
        L"\r\n"
        L"【最重要：意訳の絶対禁止】\r\n"
        L"タイポの修正と意訳は全く異なる行為です。\r\n"
        L"- タイポ修正 = ユーザーが意図したキー入力を復元する（許可）\r\n"
        L"- 意訳 = ユーザーの文意を「より良い表現」に変える（絶対禁止）\r\n"
        L"\r\n"
        L"以下を厳守してください：\r\n"
        L"- ユーザーの言い回しを「より自然な表現」に変えてはならない\r\n"
        L"- ユーザーが書いていない情報・単語・文を追加してはならない\r\n"
        L"- ユーザーが書いた意味のある単語・文を省略してはならない\r\n"
        L"- ユーザーが並べた語順を入れ替えてはならない\r\n"
        L"- 敬体（です・ます）を常体（だ・である）に変えたり、その逆も禁止\r\n"
        L"- 文章の意味を推測して「こう言いたいのだろう」と内容を補わない\r\n"
        L"- ユーザーが打っていない句読点を追加しない\r\n"
        L"\r\n"
        L"【判断基準】\r\n"
        L"迷った場合は「ユーザーの入力に最も忠実な変換」を選ぶ。自然さより忠実さを優先する。";
}
