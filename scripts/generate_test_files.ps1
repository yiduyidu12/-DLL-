# 海量文件生成脚本 — 用于验证流控机制
# 用法:
#   .\generate_test_files.ps1                           # 默认: 5万文件
#   .\generate_test_files.ps1 -FileCount 200000         # 20万文件 (高压力)
#   .\generate_test_files.ps1 -TargetDir "D:\perf_test" # 指定目录
param(
    [string]$TargetDir = ".\__perf_test",
    [int]$FileCount = 50000,
    [int]$DirsPerLevel = 50
)

$ErrorActionPreference = "Stop"
$startTime = Get-Date

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  海量测试文件生成器" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "目标目录:   $TargetDir"
Write-Host "目标文件数: $FileCount"
Write-Host "每层子目录: $DirsPerLevel"
Write-Host "========================================"

# 清理旧目录
if (Test-Path $TargetDir) {
    Write-Host "`n[1/3] 清理旧测试目录..." -ForegroundColor Yellow
    Remove-Item -Recurse -Force $TargetDir -ErrorAction SilentlyContinue
}

# 创建目录结构
Write-Host "[2/3] 创建目录结构..." -ForegroundColor Yellow
New-Item -ItemType Directory -Path $TargetDir -Force | Out-Null

# 计算需要的目录数：每个目录约 30 个文件
$dirsNeeded = [math]::Ceiling($FileCount / 30)
$depth = [math]::Ceiling([math]::Log($dirsNeeded) / [math]::Log($DirsPerLevel))
if ($depth -lt 1) { $depth = 1 }
if ($depth -gt 4) { $depth = 4 }

Write-Host "  将创建 ${depth} 层目录结构，约 $dirsNeeded 个子目录" -ForegroundColor Gray

# BFS 构建目录树
$leafDirs = @($TargetDir)
for ($level = 1; $level -le $depth; $level++) {
    $nextDirs = @()
    $created = 0
    foreach ($parent in $leafDirs) {
        for ($i = 1; $i -le $DirsPerLevel; $i++) {
            $dirPath = Join-Path $parent "d_$i"
            [System.IO.Directory]::CreateDirectory($dirPath) | Out-Null
            $nextDirs += $dirPath
            $created++
            if ($created % 500 -eq 0) {
                Write-Host "    已创建 $created 个目录..." -ForegroundColor Gray
            }
        }
    }
    $leafDirs = $nextDirs
    Write-Host "  第 ${level} 层完成，共 $created 个目录" -ForegroundColor Gray
}

Write-Host "  目录结构就绪: $($leafDirs.Count) 个叶子目录" -ForegroundColor Green

# 批量生成文件（直接 .NET API，无 Job 开销）
Write-Host "[3/3] 生成文件..." -ForegroundColor Yellow
$filesCreated = 0
$dirIndex = 0
$dirCount = $leafDirs.Count
$filesPerDir = [math]::Ceiling($FileCount / $dirCount)

foreach ($dir in $leafDirs) {
    $remaining = $FileCount - $filesCreated
    $n = [math]::Min($filesPerDir, $remaining)
    if ($n -le 0) { break }

    for ($j = 0; $j -lt $n; $j++) {
        $filePath = Join-Path $dir "f_${filesCreated}.dat"
        try {
            [System.IO.File]::WriteAllText($filePath, "x")
        } catch {
            # 忽略个别创建失败
        }
        $filesCreated++
    }

    # 进度报告（每 2000 文件报告一次）
    if ($filesCreated % 2000 -eq 0 -or $filesCreated -ge $FileCount) {
        $elapsed = ((Get-Date) - $startTime).TotalSeconds
        $rate = if ($elapsed -gt 0) { [int]($filesCreated / $elapsed) } else { 0 }
        $pct = [int](($filesCreated / $FileCount) * 100)
        Write-Host "  已生成 $filesCreated / $FileCount (${pct}%) — ${rate} 文件/秒" -ForegroundColor Gray
    }
}

$totalTime = ((Get-Date) - $startTime).TotalSeconds
$totalSize = 0
try {
    $totalSize = (Get-ChildItem -Path $TargetDir -Recurse -File -ErrorAction SilentlyContinue |
                  Measure-Object -Property Length -Sum).Sum
} catch { }

Write-Host "`n========================================" -ForegroundColor Green
Write-Host "  生成完成!" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Green
Write-Host "  文件总数: $FileCount" -ForegroundColor Green
Write-Host "  目录总数: $($leafDirs.Count)" -ForegroundColor Green
Write-Host "  总大小:   $([math]::Round($totalSize / 1MB, 2)) MB" -ForegroundColor Green
Write-Host "  耗时:     $([math]::Round($totalTime, 1)) 秒" -ForegroundColor Green
Write-Host "  平均速度: $([int]($FileCount / $totalTime)) 文件/秒" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Green
Write-Host "`n现在启动 TestApp.exe，选择目录: $((Resolve-Path $TargetDir).Path)" -ForegroundColor Cyan
Write-Host "观察要点:" -ForegroundColor White
Write-Host "  1. 状态栏是否出现 '(显示采样中...)' — 说明流控已激活" -ForegroundColor White
Write-Host "  2. 结果区是否显示 '(显示采样: N 条未展示)'" -ForegroundColor White
Write-Host "  3. 整个扫描过程 UI 是否始终保持响应" -ForegroundColor White
