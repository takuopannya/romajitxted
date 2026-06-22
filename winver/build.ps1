[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

$appName = "RomajiTxted"
$electronZip = "C:\Users\shin1\AppData\Local\electron\Cache\c94f2fc32e1fb05767f75322ea533eeb9828155f017ec184140930a3ec825e81\electron-v31.7.7-win32-x64.zip"
$outDir = "j:\romajitxted\winver\dist\$appName-win32-x64"

Write-Host "--- ビルド開始 ---"

if (Test-Path $outDir) {
    Write-Host "既存の出力ディレクトリを削除しています..."
    Remove-Item -Recurse -Force $outDir
}
New-Item -ItemType Directory -Path $outDir -Force | Out-Null

Write-Host "Electron ランタイムを展開しています..."
Expand-Archive -Path $electronZip -DestinationPath $outDir -Force

Write-Host "実行ファイルをリネームしています..."
Rename-Item -Path "$outDir\electron.exe" -NewName "$appName.exe"

Write-Host "リソースディレクトリを作成しています..."
$appDir = "$outDir\resources\app"
New-Item -ItemType Directory -Path $appDir -Force | Out-Null

Write-Host "アプリケーションファイルをコピーしています..."
Copy-Item -Path "main.js", "preload.js", "index.html", "package.json" -Destination $appDir -Force

# package.json の scripts など不要なフィールドを整理（警告回避のため）
# 今回は単純なコピーのみで動作に影響しないため、そのままコピーで問題ありません。

Write-Host "--- ビルド完了 ---"
Write-Host "出力先: $outDir\$appName.exe"
