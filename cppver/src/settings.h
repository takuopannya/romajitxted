#pragma once
#include "app.h"
#include <string>

// Load settings from %APPDATA%\RomajiTxted\settings.json
void LoadSettings(AppSettings& settings);

// Save settings to %APPDATA%\RomajiTxted\settings.json
void SaveSettings(const AppSettings& settings);

// Get settings file path
std::wstring GetSettingsPath();
