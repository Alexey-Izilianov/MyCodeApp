# Портативная сборка для Windows: exe, демо-плагин, библиотеки Qt и MinGW,
# пустая папка data (с ней программа хранит настройки рядом с собой) -> zip.
# Нужны собранный Release и windeployqt в PATH (или -QtBin).
#   ./tools/package.ps1 [-BuildDir build/Release] [-OutDir dist] [-QtBin C:/Qt/6.11.2/mingw_64/bin]
param(
    [string]$BuildDir = "build/Release",
    [string]$OutDir = "dist",
    [string]$QtBin = "",
    [switch]$SkipModel # без GGUF: GitHub ограничивает вложения релиза 2 ГБ
)
$ErrorActionPreference = "Stop"

$version = (Select-String -Path "$PSScriptRoot/../CMakeLists.txt" -Pattern 'project\(MyCodeApp VERSION ([0-9.]+)').Matches[0].Groups[1].Value
$name = "MyCodeApp-$version-win64"
$stage = Join-Path $OutDir $name
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Force "$stage/plugins", "$stage/data" | Out-Null

Copy-Item "$BuildDir/appMyCodeApp.exe" $stage
Copy-Item "$BuildDir/plugins/*.dll" "$stage/plugins"
Set-Content "$stage/data/README.txt" "Папка data включает портативный режим: настройки, сессии и темы хранятся здесь. Удалите её, чтобы данные хранились в профиле пользователя."

# Встроенная модель ассистента (GGUF): если рядом со сборкой есть models/ — едет в пакет.
# Без неё ассистент работает через Ollama/сервер, а здесь просит файл модели
$modelDir = Join-Path $BuildDir "models"
if ($SkipModel) {
    Write-Host "модель в пакет не кладём (-SkipModel)"
} elseif (Test-Path $modelDir) {
    New-Item -ItemType Directory -Force (Join-Path $stage "models") | Out-Null
    Copy-Item "$modelDir/*.gguf" (Join-Path $stage "models")
    $modelSize = [math]::Round(((Get-ChildItem "$stage/models/*.gguf" | Measure-Object Length -Sum).Sum / 1GB), 2)
    Write-Host "модель в пакете: $modelSize ГБ"
} else {
    Write-Warning "models/*.gguf не найден в $BuildDir — ассистент в пакете будет без встроенной модели"
}

$windeployqt = if ($QtBin) { Join-Path $QtBin "windeployqt.exe" } else { (Get-Command windeployqt).Source }
& $windeployqt --release --qmldir "$PSScriptRoot/.." --no-translations --compiler-runtime "$stage/appMyCodeApp.exe"
if ($LASTEXITCODE -ne 0) { throw "windeployqt завершился с кодом $LASTEXITCODE" }

$zip = Join-Path $OutDir "$name-portable.zip"
if (Test-Path $zip) { Remove-Item $zip }
Compress-Archive -Path $stage -DestinationPath $zip
Write-Host "Готово: $zip"
