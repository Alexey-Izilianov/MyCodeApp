# Скачивает GGUF модели ассистента в models рядом со сборкой.
# Запуск: powershell -File tools\fetch-model.ps1 [-To build/Release]
# Сеть используется только по явному запуску этого скрипта.
# Источник — зеркало lmstudio-community на Hugging Face; лицензия Apache 2.0.
param(
    [string]$To = "build/Release"
)
$ErrorActionPreference = "Stop"

$url = "https://huggingface.co/lmstudio-community/Qwen3-4B-Instruct-2507-GGUF/resolve/main/Qwen3-4B-Instruct-2507-Q4_K_M.gguf"
$sha256 = $null # зеркала не публикуют; проверяем только размер

$outDir = Join-Path $To "models"
New-Item -ItemType Directory -Force $outDir | Out-Null
$file = Join-Path $outDir "Qwen3-4B-Instruct-2507-Q4_K_M.gguf"
$expectedBytes = 2497280448

if ((Test-Path $file) -and ((Get-Item $file).Length -ge $expectedBytes)) {
    Write-Host "Модель уже на месте: $file"
    exit 0
}

Write-Host "Скачивание модели (~2,3 ГБ) в $file ..."
$part = "$file.part"
curl.exe -L -o $part $url
if ($LASTEXITCODE -ne 0) { throw "curl завершился с кодом $LASTEXITCODE" }

$size = (Get-Item $part).Length
if ($size -lt $expectedBytes) { throw "Файл короче ожидаемого: $size из $expectedBytes" }
Move-Item $part $file -Force
Write-Host "Готово: $file"
