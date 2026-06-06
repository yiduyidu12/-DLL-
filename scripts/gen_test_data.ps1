param(
    [string]$RootDir = "$PSScriptRoot\..\__test_scan_data"
)

Write-Host "Creating test data at: $RootDir" -ForegroundColor Cyan

# clean up
if (Test-Path $RootDir) { Remove-Item $RootDir -Recurse -Force }

New-Item -ItemType Directory -Path $RootDir -Force | Out-Null

# level 1 files
$rng = New-Object System.Random
foreach ($i in 1..5) {
    $sizeKB = Get-Random -Min 1 -Max 512
    $bytes = New-Object byte[] ($sizeKB * 1024)
    $rng.NextBytes($bytes)
    [IO.File]::WriteAllBytes("$RootDir\file_level1_$i.dat", $bytes)
    Write-Host "  [+] file_level1_$i.dat ($sizeKB KB)"
}

# level 1 dirs with files
foreach ($d in @("Docs", "Images", "Source", "Backup", "Logs")) {
    $dir = "$RootDir\$d"
    New-Item -ItemType Directory -Path $dir -Force | Out-Null
    Write-Host "  [D] $d"

    foreach ($i in 1..(Get-Random -Min 3 -Max 8)) {
        $sizeKB = Get-Random -Min 1 -Max 256
        $bytes = New-Object byte[] ($sizeKB * 1024)
        $rng.NextBytes($bytes)
        [IO.File]::WriteAllBytes("$dir\${d}_file_$i.dat", $bytes)
    }
}

# nested subdirs
$nested = "$RootDir\Source\src\core"
New-Item -ItemType Directory -Path $nested -Force | Out-Null
Write-Host "  [D] Source\src\core"

foreach ($i in 1..4) {
    $sizeKB = Get-Random -Min 10 -Max 200
    $bytes = New-Object byte[] ($sizeKB * 1024)
    $rng.NextBytes($bytes)
    [IO.File]::WriteAllBytes("$nested\core_$i.cpp", $bytes)
}

$nested2 = "$RootDir\Source\src\utils"
New-Item -ItemType Directory -Path $nested2 -Force | Out-Null
Write-Host "  [D] Source\src\utils"

foreach ($i in 1..3) {
    $sizeKB = Get-Random -Min 5 -Max 100
    $bytes = New-Object byte[] ($sizeKB * 1024)
    $rng.NextBytes($bytes)
    [IO.File]::WriteAllBytes("$nested2\util_$i.cpp", $bytes)
}

# deep nested empty dir
$deep = "$RootDir\Logs\archive\2024\12\old"
New-Item -ItemType Directory -Path $deep -Force | Out-Null
Write-Host "  [D] Logs\archive\2024\12\old (empty)"

# special name files
"hello world" | Out-File "$RootDir\test with spaces.txt" -Encoding utf8
"line1`nline2`nline3" | Out-File "$RootDir\multiline.txt" -Encoding utf8
"{}" | Out-File "$RootDir\config.json" -Encoding utf8

# large file (5MB)
$largeFile = "$RootDir\Backup\large_backup.bin"
$sizeMB = 5
$bytes = New-Object byte[] ($sizeMB * 1024 * 1024)
$rng.NextBytes($bytes)
[IO.File]::WriteAllBytes($largeFile, $bytes)
Write-Host "  [+] Backup\large_backup.bin (5 MB)"

# read-only dir (skip test)
$readonly = "$RootDir\readonly_dir"
New-Item -ItemType Directory -Path $readonly -Force | Out-Null
"secret data" | Out-File "$readonly\secret.txt" -Encoding utf8
# 设置目录为只读属性
(Get-Item $readonly).Attributes = [System.IO.FileAttributes]::ReadOnly

Write-Host ""
Write-Host "Done! Total files: $((Get-ChildItem $RootDir -Recurse -File).Count)" -ForegroundColor Green
Write-Host "Total dirs: $((Get-ChildItem $RootDir -Recurse -Directory).Count)" -ForegroundColor Green
Write-Host "Root: $RootDir" -ForegroundColor Green
