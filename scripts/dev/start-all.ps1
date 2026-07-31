$ErrorActionPreference = "Stop"

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path

# ── 1. 检查 PostgreSQL ──────────────────────────────────────────────
$psqlExe = $env:PSQL_EXE
if ([string]::IsNullOrWhiteSpace($psqlExe)) {
  $psqlCandidates = @(
    "D:\PostgreSQL\18\bin\psql.exe",
    "C:\Program Files\PostgreSQL\18\bin\psql.exe"
  )
  $psqlExe = $psqlCandidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
}
if ([string]::IsNullOrWhiteSpace($psqlExe)) {
  Write-Warning "未找到 psql；请设置环境变量 PSQL_EXE 指向 PostgreSQL 客户端，或确保 psql 在 PATH 中。"
} else {
  Write-Host "检查 PostgreSQL … " -NoNewline
  $databaseUrl = $env:BRIDGE_REPORT_DATABASE_URL
  if ([string]::IsNullOrWhiteSpace($databaseUrl)) {
    $databaseUrl = "postgresql://bridge_report:bridge_report_dev@127.0.0.1:5432/bridge_report_system"
  }
  try {
    & $psqlExe $databaseUrl -c "select 1" 2>&1 | Out-Null
    Write-Host "OK" -ForegroundColor Green
  } catch {
    Write-Error "PostgreSQL 不可达 ($databaseUrl)；请先启动数据库。"
    exit 1
  }
}

# ── 2. 启动三个服务（各自独立窗口）────────────────────────────────
$services = @(
  @{
    Name    = "Python 工具服务"
    Script  = "start-python-tools.ps1"
    Dir     = "tools-python"
    Port    = 18081
    Color   = "Cyan"
  },
  @{
    Name    = "C++ 主服务"
    Script  = "start-cpp-backend.ps1"
    Dir     = "backend-cpp"
    Port    = 18080
    Color   = "Green"
  },
  @{
    Name    = "前端开发服务"
    Script  = "start-frontend.ps1"
    Dir     = "frontend"
    Port    = 5173
    Color   = "Magenta"
  }
)

foreach ($service in $services) {
  $windowTitle = "$($service.Name) — bridge-report-system"
  $scriptPath = Join-Path $PSScriptRoot $service.Script
  $workingDir = Join-Path $repositoryRoot $service.Dir

  if (-not (Test-Path -LiteralPath $scriptPath)) {
    Write-Error "启动脚本不存在: $scriptPath"
    exit 1
  }

  Write-Host "启动 $($service.Name)（:$($service.Port)）… " -ForegroundColor $service.Color -NoNewline

  Start-Process pwsh `
    -WorkingDirectory $workingDir `
    -ArgumentList "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", "`"$scriptPath`"" `
    -WindowStyle Normal

  Write-Host "已启动" -ForegroundColor $service.Color

  # 各服务之间给一点初始化时间
  Start-Sleep -Seconds 1
}

# ── 3. 输出访问地址 ─────────────────────────────────────────────────
Write-Host ""
Write-Host "══════════════════════════════════════════" -ForegroundColor White
Write-Host "  所有服务已启动" -ForegroundColor White
Write-Host "══════════════════════════════════════════" -ForegroundColor White
Write-Host ""
Write-Host "  前端        http://127.0.0.1:5173" -ForegroundColor Cyan
Write-Host "  C++ 后端    http://127.0.0.1:18080" -ForegroundColor Green
Write-Host "  Python 工具 http://127.0.0.1:18081" -ForegroundColor Magenta
Write-Host "  PostgreSQL  http://127.0.0.1:5432" -ForegroundColor DarkGray
Write-Host ""
Write-Host "  关闭时直接关闭各窗口即可。" -ForegroundColor DarkGray
Write-Host ""

# ── 4. 自动打开浏览器 ──────────────────────────────────────────────
Start-Process "http://127.0.0.1:5173"
