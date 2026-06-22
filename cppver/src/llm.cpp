#include "llm.h"
#include "http.h"
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <sstream>

using json = nlohmann::json;

static std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(len, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], len, nullptr, nullptr);
    return s;
}

// Build full system prompt with user dictionary appended
static std::string BuildSystemPrompt(const AppState& app) {
    std::string prompt = WideToUtf8(app.settings.systemPrompt);

    std::string dict = WideToUtf8(app.settings.userDictionary);
    if (!dict.empty()) {
        std::istringstream iss(dict);
        std::string line;
        std::vector<std::string> entries;
        while (std::getline(iss, line)) {
            // Trim
            while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) line.erase(0, 1);
            while (!line.empty() && (line.back() == ' ' || line.back() == '\t' || line.back() == '\r')) line.pop_back();
            if (!line.empty() && line.find(':') != std::string::npos) {
                entries.push_back(line);
            }
        }
        if (!entries.empty()) {
            prompt += "\n\n【固有名詞・単語の固定辞書（最優先ルール）】";
            prompt += "\n以下のローマ字入力パターン（左）は、右側の日本語に必ずマッピングして変換してください。入力が曖昧・タイポであっても、この辞書マッピングを最優先します。";
            for (auto& e : entries) {
                prompt += "\n- " + e;
            }
        }
    }

    return prompt;
}

// ── Google Gemini ──
static std::string CallGoogle(const std::string& text, const std::string& model,
                               const std::string& apiKey, const std::string& systemPrompt) {
    std::string url = "https://generativelanguage.googleapis.com/v1beta/models/"
                     + model + ":generateContent?key=" + apiKey;

    json body;
    body["system_instruction"] = {{"parts", json::array({{{"text", systemPrompt}}})}};
    body["contents"] = json::array({{{"parts", json::array({{{"text", text}}})}}});
    body["generationConfig"] = {{"temperature", 0.2}, {"maxOutputTokens", 2048}};

    auto resp = HttpPost(url, body.dump(), {
        {"Content-Type", "application/json"}
    });

    if (!resp.ok()) {
        try {
            auto err = json::parse(resp.body);
            throw std::runtime_error(err["error"]["message"].get<std::string>());
        } catch (json::exception&) {
            throw std::runtime_error("Google HTTP " + std::to_string(resp.statusCode));
        }
    }

    auto data = json::parse(resp.body);
    std::string result = data["candidates"][0]["content"]["parts"][0]["text"].get<std::string>();
    while (!result.empty() && (result.front() == ' ' || result.front() == '\n')) result.erase(0, 1);
    while (!result.empty() && (result.back() == ' ' || result.back() == '\n')) result.pop_back();
    return result;
}

// ── Ollama ──
static std::string CallOllama(const std::string& text, const std::string& model,
                               const std::string& baseUrl, const std::string& systemPrompt) {
    std::string url = baseUrl;
    while (!url.empty() && url.back() == '/') url.pop_back();
    url += "/api/chat";

    json body;
    body["model"] = model;
    body["stream"] = false;
    body["messages"] = json::array({
        {{"role", "system"}, {"content", systemPrompt}},
        {{"role", "user"}, {"content", text}}
    });
    body["options"] = {{"temperature", 0.2}};

    auto resp = HttpPost(url, body.dump(), {
        {"Content-Type", "application/json"}
    });

    if (!resp.ok()) {
        throw std::runtime_error("Ollama HTTP " + std::to_string(resp.statusCode)
                                 + ": " + resp.body.substr(0, 100));
    }

    auto data = json::parse(resp.body);
    std::string result = data["message"]["content"].get<std::string>();
    while (!result.empty() && (result.front() == ' ' || result.front() == '\n')) result.erase(0, 1);
    while (!result.empty() && (result.back() == ' ' || result.back() == '\n')) result.pop_back();
    return result;
}

// ── Main entry ──
std::string CallLLM(const AppState& app, const std::string& inputText) {
    std::string systemPrompt = BuildSystemPrompt(app);
    const auto& s = app.settings;
    std::string apiKey = app.decryptedApiKey.empty() ? s.apiKey : app.decryptedApiKey;

    switch (s.provider) {
        case Provider::Google:
            return CallGoogle(inputText, s.model, apiKey, systemPrompt);
        case Provider::Ollama:
            return CallOllama(inputText, s.model, s.ollamaUrl, systemPrompt);
        default:
            throw std::runtime_error("Unknown provider");
    }
}

// ── Fetch Model List ──
std::vector<ModelInfo> FetchModelList(Provider provider, const std::string& apiKey,
                                      const std::string& ollamaUrl) {
    std::vector<ModelInfo> models;

    try {
        if (provider == Provider::Google) {
            auto resp = HttpGet("https://generativelanguage.googleapis.com/v1beta/models?key="
                               + apiKey + "&pageSize=100");
            if (!resp.ok()) return {};
            auto data = json::parse(resp.body);
            for (auto& m : data["models"]) {
                if (m.contains("supportedGenerationMethods")) {
                    auto methods = m["supportedGenerationMethods"];
                    bool hasGenerate = false;
                    for (auto& method : methods) {
                        if (method == "generateContent") { hasGenerate = true; break; }
                    }
                    if (hasGenerate) {
                        std::string name = m["name"];
                        if (name.find("models/") == 0) name = name.substr(7);
                        std::string display = m.value("displayName", name);
                        models.push_back({name, display});
                    }
                }
            }
        } else if (provider == Provider::Ollama) {
            std::string url = ollamaUrl;
            while (!url.empty() && url.back() == '/') url.pop_back();
            url += "/api/tags";
            auto resp = HttpGet(url);
            if (!resp.ok()) return {};
            auto data = json::parse(resp.body);
            for (auto& m : data["models"]) {
                std::string name = m["name"];
                models.push_back({name, name});
            }
        }
    } catch (...) {
        // Silently fail
    }

    return models;
}
