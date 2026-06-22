#include "settings.h"
#include <windows.h>
#include <shlobj.h>
#include <fstream>
#include <filesystem>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

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

static std::string ProviderToString(Provider p) {
    switch (p) {
        case Provider::OpenAI:    return "openai";
        case Provider::Anthropic: return "anthropic";
        case Provider::Google:    return "google";
        case Provider::Ollama:    return "ollama";
        default:                  return "openai";
    }
}

static Provider StringToProvider(const std::string& s) {
    if (s == "anthropic") return Provider::Anthropic;
    if (s == "google")    return Provider::Google;
    if (s == "ollama")    return Provider::Ollama;
    return Provider::OpenAI;
}

static std::string LayoutToString(Layout l) {
    switch (l) {
        case Layout::HorizontalReverse: return "horizontal-reverse";
        case Layout::Vertical:          return "vertical";
        case Layout::VerticalReverse:   return "vertical-reverse";
        default:                        return "horizontal";
    }
}

static Layout StringToLayout(const std::string& s) {
    if (s == "horizontal-reverse") return Layout::HorizontalReverse;
    if (s == "vertical")           return Layout::Vertical;
    if (s == "vertical-reverse")   return Layout::VerticalReverse;
    return Layout::Horizontal;
}

std::wstring GetSettingsPath() {
    wchar_t* appData = nullptr;
    SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appData);
    std::wstring path = appData;
    CoTaskMemFree(appData);
    path += L"\\RomajiTxted";
    CreateDirectoryW(path.c_str(), nullptr);
    path += L"\\settings.json";
    return path;
}

void LoadSettings(AppSettings& settings) {
    std::wstring path = GetSettingsPath();
    std::ifstream ifs{std::filesystem::path(path)};
    if (!ifs.is_open()) {
        // Use defaults
        settings.systemPrompt = GetDefaultSystemPrompt();
        return;
    }

    try {
        json j = json::parse(ifs);

        if (j.contains("provider"))    settings.provider = StringToProvider(j["provider"]);
        if (j.contains("model"))       settings.model = j["model"];
        if (j.contains("apiKey"))      settings.apiKey = j["apiKey"].dump(); // keep as JSON string
        if (j.contains("isEncrypted")) settings.isEncrypted = j["isEncrypted"];
        if (j.contains("ollamaUrl"))   settings.ollamaUrl = j["ollamaUrl"];
        if (j.contains("layout"))      settings.layout = StringToLayout(j["layout"]);
        if (j.contains("userDictionary"))
            settings.userDictionary = Utf8ToWide(j["userDictionary"]);
        if (j.contains("systemPrompt"))
            settings.systemPrompt = Utf8ToWide(j["systemPrompt"]);
        if (j.contains("systemPromptVersion"))
            settings.systemPromptVersion = j["systemPromptVersion"];

        // apiKey handling: if encrypted, store the raw JSON object string
        if (settings.isEncrypted && j.contains("apiKey") && j["apiKey"].is_object()) {
            settings.apiKey = j["apiKey"].dump();
        } else if (j.contains("apiKey") && j["apiKey"].is_string()) {
            settings.apiKey = j["apiKey"].get<std::string>();
        }

        // Default system prompt if version mismatch
        if (settings.systemPromptVersion < 2) {
            settings.systemPrompt = GetDefaultSystemPrompt();
            settings.systemPromptVersion = 2;
        }
        if (settings.systemPrompt.empty()) {
            settings.systemPrompt = GetDefaultSystemPrompt();
        }
    } catch (...) {
        settings.systemPrompt = GetDefaultSystemPrompt();
    }
}

void SaveSettings(const AppSettings& settings) {
    json j;
    j["provider"] = ProviderToString(settings.provider);
    j["model"] = settings.model;
    j["isEncrypted"] = settings.isEncrypted;
    j["ollamaUrl"] = settings.ollamaUrl;
    j["layout"] = LayoutToString(settings.layout);
    j["userDictionary"] = WideToUtf8(settings.userDictionary);
    j["systemPrompt"] = WideToUtf8(settings.systemPrompt);
    j["systemPromptVersion"] = settings.systemPromptVersion;

    // apiKey: if encrypted, parse back to JSON object; if plain, store as string
    if (settings.isEncrypted && !settings.apiKey.empty()) {
        try {
            j["apiKey"] = json::parse(settings.apiKey);
        } catch (...) {
            j["apiKey"] = settings.apiKey;
        }
    } else {
        j["apiKey"] = settings.apiKey;
    }

    std::wstring path = GetSettingsPath();
    std::ofstream ofs{std::filesystem::path(path)};
    if (ofs.is_open()) {
        ofs << j.dump(2);
    }
}
