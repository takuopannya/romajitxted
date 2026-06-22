#pragma once
#include "app.h"

// Initialize and create the main window
bool InitUI(HINSTANCE hInstance, AppState& app);

// Show settings dialog (modal)
void ShowSettingsDialog(HWND hParent, AppState& app);

// Show password prompt dialog (modal), returns password or empty
std::wstring ShowPasswordDialog(HWND hParent);

// Update status bar text
void UpdateStatusBar(AppState& app);

// Update statistics display
void UpdateStats(AppState& app);

// Set status indicator
void SetStatus(AppState& app, StatusState state, const wchar_t* text);

// Show toast notification
void ShowToast(HWND hWnd, const wchar_t* msg);

// Apply layout
void ApplyLayout(AppState& app);

// Trigger conversion
void DoConvert(AppState& app);
