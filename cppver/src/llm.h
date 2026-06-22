#pragma once
#include "app.h"
#include <string>
#include <vector>

// Call LLM and return the converted text
// This is synchronous — call from worker thread
std::string CallLLM(const AppState& app, const std::string& inputText);

// Fetch model list from provider (returns list of ModelInfo)
std::vector<ModelInfo> FetchModelList(Provider provider, const std::string& apiKey,
                                      const std::string& ollamaUrl = "");
