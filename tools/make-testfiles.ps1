# Создаёт тестовые файлы для ручной проверки MyCodeApp в папке testdata.
# Запуск: powershell -ExecutionPolicy Bypass -File tools\make-testfiles.ps1
$ErrorActionPreference = "Stop"
$out = Join-Path $PSScriptRoot "..\..\testdata"

function W([string]$name, [byte[]]$bytes) {
    [IO.File]::WriteAllBytes((Join-Path $out $name), $bytes)
    Write-Host "  $name"
}

# 1. Windows-1251
$cp1251 = [Text.Encoding]::GetEncoding(1251)
$cpText = "Привет, редактор!`r`nВторая строка по-русски.`r`nТретья - с ёлками-палками и N.`r`n"
W "cp1251-sample.txt" $cp1251.GetBytes($cpText)

# 2. UTF-16 LE с BOM
$uni = [Text.Encoding]::Unicode
$uText = "Юникод в два байта.`nВторая строка.`n"
W "utf16le-sample.txt" ($uni.GetPreamble() + $uni.GetBytes($uText))

# 3. Latin-1
$lat = [Text.Encoding]::GetEncoding(28591)
$lText = "cafe nave resume`nsecond line: creme brulee`n"
W "latin1-sample.txt" $lat.GetBytes($lText)

# 4. Смешанные окончания строк и длинная строка
$eol = "обычная строка 1`nобычная строка 2`ncrlf-строка 1`r`ncrlf-строка 2`r`ncr-only строка`rдлинная: "
$eol += ("тест " * 2000)
$eol += "`n"
W "endings-mix.txt" [Text.Encoding]::UTF8.GetBytes($eol)

# 5. Гигантские строки для regex: две строки по 1,2 млн 'a' и строка с needle
$part = "a" * 1200000
$re = $part + "`n" + $part + "`nneedle`n"
W "regex-flood.txt" [Text.Encoding]::UTF8.GetBytes($re)

# 6. Файл для проверки ReDoS-предохранителя
W "redos.txt" [Text.Encoding]::UTF8.GetBytes(("a" * 50000) + "b")

# 7. Markdown с вложенным кодом
$mdLines = @(
  "# Заголовок",
  "",
  "Список:",
  "- один",
  "- два",
  "",
  "Таблица:",
  "| А | Б |",
  "|---|---|",
  "| 1 | 2 |",
  "",
  "```cpp",
  "int main() { return 0; } // код на C++",
  "```",
  "",
  "Ссылка: https://example.org **жирный** *курсив*"
)
W "doc-sample.md" [Text.Encoding]::UTF8.GetBytes(($mdLines -join "`n") + "`n")

# 8. Папка-проект для Git и поиска по проекту
$repo = Join-Path $out "git-sample"
if (-not (Test-Path $repo)) { New-Item -ItemType Directory -Path $repo | Out-Null }
[IO.File]::WriteAllText((Join-Path $repo "alpha.cpp"), "int funcA() { return 1; }`nint funcB() { return funcA(); }`n", [Text.Encoding]::UTF8)
[IO.File]::WriteAllText((Join-Path $repo "beta.cpp"), "// файл для поиска по проекту`nint beta = 1;`n", [Text.Encoding]::UTF8)
[IO.File]::WriteAllText((Join-Path $repo "README.md"), "пример проекта для ручных проверок`n", [Text.Encoding]::UTF8)
Write-Host "  git-sample\alpha.cpp beta.cpp README.md"
Write-Host "Готово: $out"
