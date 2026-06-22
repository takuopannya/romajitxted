# RomajiTxted - ローマ字→日本語エディタ

LLM（大規模言語モデル）の高度な推論能力を活用し、タイポや不完全なローマ字入力からでも自然な日本語へ文脈を推測して変換する、新感覚のテキストエディタ・入力支援ツールです。

利用環境や好みに合わせて、**3つのバージョン**が用意されています。

---

## 📁 提供バージョン

### 1. C++ ネイティブ版 (`cppver/`)
- **特徴**: Win32 API を用いてネイティブ実装された、超軽量かつ高速に動作するWindowsデスクトップアプリです。メモリやCPUリソースの消費を最小限に抑え、素早い起動が可能です。
- **機能**: 
  - Gemini API および ローカルOllama に対応。
  - マスターパスワードによる API キーの暗号化保存機能。
  - モデル一覧の動的取得、レイアウト切り替え、ユーザー辞書、プロンプトのカスタマイズ機能。
- **ビルド方法**:
  - `CMakeLists.txt` を使用して MinGW または MSVC でビルドします。
  - MinGWの例: `cd build-mingw && cmake .. -G "MinGW Makefiles" && cmake --build . --config Release`

### 2. Electron デスクトップ版 (`winver/`)
- **特徴**: Web技術（HTML/JS/CSS）をベースに構築されたデスクトップアプリです。Web版のリッチなUIをそのままネイティブアプリのような感覚で使用できます。
- **機能**: HTML版の全ての機能を備えつつ、Electronによってデスクトップ上で独立したウィンドウとして動作します。
- **実行方法**:
  - `cd winver`
  - `npm install`
  - `npm start` （開発時）
  - パッケージ化には同梱のビルドスクリプト（`build.ps1`など）を使用します。

### 3. ブラウザ（Web）版 (`htmlver/`)
- **特徴**: インストール不要で、ブラウザのみで動作する軽量なシングルページアプリケーション（SPA）です。
- **機能**: `index.html` をブラウザで開くだけで即座にLLM変換機能を利用可能です。
- **実行方法**: `htmlver/index.html` を任意のWebブラウザで開くか、ローカルサーバー経由でホストしてください。

---

## ✨ 主な機能（全バージョン共通）
* **高精度なLLM変換**: Gemini / Ollama のAPIを利用し、ローマ字入力（例: `kyouhaitenkigaiine`）を文脈を読んだ自然な日本語（`今日は天気がいいね`）へ変換します。
* **柔軟なレイアウト設定**: 入力ペインと出力ペインを「左右」「左右反転」「上下」「上下反転」に即座に切り替え可能。
* **フォントサイズのリアルタイム調整**: 画面上の `+` / `-` ボタンでテキストサイズを動的に変更し、快適な表示サイズに合わせることができます。
* **ユーザー辞書 & プロンプトカスタマイズ**: 独自の単語マッピングや、AIへの変換指示（システムプロンプト）を自由に変更できます。

## ⚠️ 注意事項
* **Gemini API** を使用する場合、事前に [Google AI Studio](https://aistudio.google.com/) 等にて API キーを取得しておく必要があります。
* **ローカル Ollama** を使用する場合は、事前に Ollama をインストールし、サーバー（通常は `http://127.0.0.1:11434`）を起動しておく必要があります。

---
<br>

# RomajiTxted - Romaji to Japanese LLM Editor

An innovative text editor and typing assistant that utilizes the advanced reasoning capabilities of Large Language Models (LLMs) to accurately convert imperfect Romaji input, including typos, into natural and context-aware Japanese text.

Depending on your environment and preferences, **three different versions** are available.

---

## 📁 Available Versions

### 1. C++ Native Version (`cppver/`)
- **Features**: An ultra-lightweight and lightning-fast Windows desktop application natively implemented using the Win32 API. It consumes minimal memory and CPU resources, ensuring rapid startup.
- **Capabilities**:
  - Supports Gemini API and Local Ollama.
  - Secure API key storage via master password encryption.
  - Dynamic model list fetching, layout toggling, user dictionaries, and custom system prompts.
- **How to Build**:
  - Use `CMakeLists.txt` to build with MinGW or MSVC.
  - Example (MinGW): `cd build-mingw && cmake .. -G "MinGW Makefiles" && cmake --build . --config Release`

### 2. Electron Desktop Version (`winver/`)
- **Features**: A cross-platform desktop application built on web technologies (HTML/JS/CSS). It brings the rich UI of the Web version into a standalone native-like window.
- **Capabilities**: Includes all features from the HTML version while running as an independent desktop application via Electron.
- **How to Run**:
  - `cd winver`
  - `npm install`
  - `npm start` (For development)
  - Use included scripts (e.g., `build.ps1`) to package the application.

### 3. Browser (Web) Version (`htmlver/`)
- **Features**: A lightweight Single Page Application (SPA) that requires no installation and runs entirely within your web browser.
- **Capabilities**: Simply open `index.html` to instantly use the LLM-powered conversion features.
- **How to Run**: Open `htmlver/index.html` in any web browser or host it via a local web server.

---

## ✨ Key Features (All Versions)
* **High-Accuracy LLM Conversion**: Uses Gemini or Ollama APIs to transform basic Romaji input (e.g., `kyouhaitenkigaiine`) into natural Japanese text (`今日は天気がいいね`) by understanding the context.
* **Flexible UI Layouts**: Instantly switch the input and output panes between "Left/Right", "Right/Left", "Top/Bottom", and "Bottom/Top".
* **Real-time Font Scaling**: Dynamically adjust the text size to your comfort level using the on-screen `+` / `-` buttons.
* **Custom Dictionaries & Prompts**: Freely modify specific word mappings or customize the AI conversion instructions (System Prompts).

## ⚠️ Requirements & Notes
* To use the **Gemini API**, you must obtain an API key in advance from [Google AI Studio](https://aistudio.google.com/) or a similar platform.
* To use **Local Ollama**, you must install Ollama and have the server running in the background (typically at `http://127.0.0.1:11434`).
