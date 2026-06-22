#pragma once

// ── Main Window ──
#define IDC_INPUT_EDIT        1001
#define IDC_OUTPUT_EDIT       1002
#define IDC_STATUSBAR         1003

// ── Toolbar Buttons ──
#define IDC_BTN_CONVERT       1010
#define IDC_BTN_CLEAR         1011
#define IDC_BTN_COPY          1012
#define IDC_BTN_SAVE          1013
#define IDC_BTN_SETTINGS      1014
#define IDC_BTN_HISTORY       1015
#define IDC_BTN_WRAP          1016
#define IDC_BTN_FONT_UP       1017
#define IDC_BTN_FONT_DOWN     1018
#define IDC_BTN_EDIT_OUTPUT   1019
#define IDC_BTN_REMOVE_NL     1020
#define IDC_FONT_SIZE_LABEL   1021

// ── Settings Dialog ──
#define IDD_SETTINGS          2000
#define IDC_PROVIDER_COMBO    2001
#define IDC_APIKEY_EDIT       2002
#define IDC_MASTER_PW_EDIT    2003
#define IDC_MODEL_COMBO       2004
#define IDC_MODEL_CUSTOM      2005
#define IDC_OLLAMA_URL        2006
#define IDC_USER_DICT         2007
#define IDC_SYSTEM_PROMPT     2008
#define IDC_LAYOUT_COMBO      2009
#define IDC_BTN_FETCH_MODELS  2010
#define IDC_BTN_SAVE_SETTINGS 2011
#define IDC_BTN_CANCEL_SETTINGS 2012
#define IDC_TAB_SETTINGS      2013
#define IDC_APIKEY_LABEL      2014
#define IDC_MASTER_PW_LABEL   2015
#define IDC_OLLAMA_URL_LABEL  2016
#define IDC_STATIC_MODEL_HINT 2017

// ── Password Dialog ──
#define IDD_PASSWORD          3000
#define IDC_DECRYPT_PW_EDIT   3001
#define IDC_BTN_PW_OK         3002

// ── History Panel ──
#define IDC_HISTORY_LIST      4000
#define IDC_BTN_HISTORY_CLOSE 4001

// ── Custom Messages ──
#define WM_LLM_COMPLETE       (WM_USER + 100)
#define WM_LLM_ERROR          (WM_USER + 101)
#define WM_DLGCMD             (WM_USER + 102)

// ── Pane Headers ──
#define IDC_INPUT_HEADER      5001
#define IDC_OUTPUT_HEADER     5002
#define IDC_OUTPUT_CHARCOUNT  5003

// ── Status Dot ──
#define IDC_STATUS_DOT        5010
#define IDC_STATUS_TEXT       5011
