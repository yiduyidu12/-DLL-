<#
upload_to_github.ps1

Usage:
  .\upload_to_github.ps1 -RemoteUrl "https://github.com/USERNAME/REPO.git" -Branch main

This script will:
  - verify Git is available
  - initialize a repo if needed
  - create an initial commit if none exists
  - add or update the remote `origin`
  - push the specified branch
#>

param(
    [string]$RemoteUrl,
    [string]$Branch = "main",
    [string]$CommitMessage = "Initial commit"
)

function Fail($msg) {
    Write-Error $msg
    exit 1
}

$git = Get-Command git -ErrorAction SilentlyContinue
if (-not $git) {
    Fail "Git not found. Install Git for Windows: https://git-scm.com/download/win"
}

# Test running git to detect the local error seen previously
try {
    git --version 2>$null | Out-Null
} catch {
    Fail "Git command failed when executed. Please reinstall Git or use GitHub Desktop/VS Code."
}

# Work in the current directory (where the script is located)
Set-Location -Path (Split-Path -Path $MyInvocation.MyCommand.Definition -Parent)

# If not inside a git repo, initialize
$isRepo = $false
try {
    $res = git rev-parse --is-inside-work-tree 2>$null
    if ($res -eq 'true') { $isRepo = $true }
} catch { $isRepo = $false }

if (-not $isRepo) {
    Write-Output "Initializing new git repository..."
    git init
    if ($LASTEXITCODE -ne 0) { Fail "git init failed" }
}

# Create a commit if there are no commits yet
$hasCommit = $false
try {
    git rev-parse --verify HEAD >$null 2>&1
    if ($LASTEXITCODE -eq 0) { $hasCommit = $true }
} catch { $hasCommit = $false }

if (-not $hasCommit) {
    git add -A
    if ($LASTEXITCODE -ne 0) { Fail "git add failed" }
    git commit -m "$CommitMessage"
    if ($LASTEXITCODE -ne 0) { Fail "git commit failed" }
}

if ([string]::IsNullOrWhiteSpace($RemoteUrl)) {
    Write-Output "No remote URL provided. Create a GitHub repository, then run this script with -RemoteUrl '<url>'"
    Write-Output "Example: .\upload_to_github.ps1 -RemoteUrl 'https://github.com/USERNAME/REPO.git' -Branch main"
    exit 0
}

# Add or update origin
try {
    git remote get-url origin >$null 2>&1
    if ($LASTEXITCODE -eq 0) {
        Write-Output "Remote 'origin' exists. Updating URL to $RemoteUrl"
        git remote set-url origin $RemoteUrl
        if ($LASTEXITCODE -ne 0) { Fail "git remote set-url failed" }
    } else {
        git remote add origin $RemoteUrl
        if ($LASTEXITCODE -ne 0) { Fail "git remote add failed" }
    }
} catch {
    # Fallback: try to add
    git remote add origin $RemoteUrl
    if ($LASTEXITCODE -ne 0) { Fail "git remote add failed" }
}

Write-Output "Ensuring branch name is '$Branch'"
git branch -M $Branch
if ($LASTEXITCODE -ne 0) { Fail "git branch -M failed" }

Write-Output "Pushing to remote origin/$Branch..."
git push -u origin $Branch
if ($LASTEXITCODE -ne 0) { Fail "git push failed. Check credentials and remote URL." }

Write-Output "Push completed. Repository available at: $RemoteUrl"
