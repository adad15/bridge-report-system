param(
  [string]$ConfigPath,
  [ValidateSet('Debug','Release')][string]$Configuration='Debug',
  [string]$BuildDirectory='backend-cpp/build/vs-debug',
  [string]$Preset='vs2022-x64-debug',
  [ValidateRange(1,600)][int]$ReadyTimeoutSeconds=60,
  [switch]$PrepareDependencies,
  [switch]$OpenBrowser
)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'common.ps1')
$config=Get-DevelopmentConfig $ConfigPath
foreach ($section in @('python_tools','cpp_server','frontend')) { Assert-PortAvailable $config.Value.$section.host $config.Value.$section.port }
if ($PrepareDependencies) { & (Join-Path $PSScriptRoot 'prepare-dependencies.ps1') }
Assert-PreparedDependencies 'python'
Assert-PreparedDependencies 'frontend'
Assert-DatabaseReady $config
& (Join-Path $PSScriptRoot 'start-cpp-backend.ps1') -ConfigPath $config.Path -Configuration $Configuration -BuildDirectory $BuildDirectory -Preset $Preset -BuildOnly
$exe=Get-BackendArtifact $BuildDirectory $Configuration
$build=Get-Content -LiteralPath (Join-Path (Split-Path $exe) 'build-info.json') -Raw | ConvertFrom-Json
$root=Get-RepositoryRoot
$python=Join-Path $root 'tools-python/.venv/Scripts/python.exe'
Push-Location (Join-Path $root 'tools-python')
try {
  $identityText=& $python -m bridge_report_tools.runtime_identity
  if ($LASTEXITCODE -ne 0) { throw 'Cannot capture Python source identity.' }
  $pythonIdentity=$identityText | ConvertFrom-Json
} finally { Pop-Location }
$launch=[guid]::NewGuid().ToString()
$logDir=Join-Path $root "logs/startup/$launch"
[IO.Directory]::CreateDirectory($logDir) | Out-Null
$names=@('BRIDGE_REPORT_LAUNCH_ID','BRIDGE_TOOLS_HOST','BRIDGE_TOOLS_PORT','VITE_BACKEND_BASE_URL')
$saved=@{}; foreach ($name in $names) { $saved[$name]=[Environment]::GetEnvironmentVariable($name) }
$owned=New-Object 'System.Collections.Generic.List[System.Diagnostics.Process]'
$stage='Python startup'
try {
  $env:BRIDGE_REPORT_LAUNCH_ID=$launch
  $env:BRIDGE_TOOLS_HOST=[string]$config.Value.python_tools.host
  $env:BRIDGE_TOOLS_PORT=[string]$config.Value.python_tools.port
  $env:VITE_BACKEND_BASE_URL=$config.BackendUrl
  $process=Start-OwnedService $python @('-m','uvicorn','bridge_report_tools.main:app','--host',$env:BRIDGE_TOOLS_HOST,'--port',$env:BRIDGE_TOOLS_PORT,'--workers','1') (Join-Path $root 'tools-python') (Join-Path $logDir 'python')
  $owned.Add($process)
  Wait-ServiceReady $process {
    $health=Invoke-RestMethod "$($config.PythonUrl)/health" -TimeoutSec 3
    Test-ServiceIdentity $health 'bridge-report-python-tools' $launch '' $pythonIdentity.source_digest
  } $ReadyTimeoutSeconds 'Python'
  $stage='C++ startup'
  $process=Start-OwnedService $exe @($config.Path) $root (Join-Path $logDir 'backend')
  $owned.Add($process)
  Wait-ServiceReady $process {
    $health=Invoke-RestMethod "$($config.BackendUrl)/health" -TimeoutSec 3
    if (-not (Test-ServiceIdentity $health 'bridge-report-cpp-backend' $launch $build.build_id '')) { return $false }
    $db=Invoke-RestMethod "$($config.BackendUrl)/health/db" -TimeoutSec 3
    $via=Invoke-RestMethod "$($config.BackendUrl)/health/tools" -TimeoutSec 3
    return ($db.status -eq 'ok' -and $db.database -eq 'reachable' -and $via.tools_status -eq 'reachable')
  } $ReadyTimeoutSeconds 'C++'
  $stage='Frontend startup'
  $process=Start-OwnedService (Find-DevelopmentTool 'node.exe') @('node_modules/vite/bin/vite.js','--host',[string]$config.Value.frontend.host,'--port',[string]$config.Value.frontend.port,'--strictPort') (Join-Path $root 'frontend') (Join-Path $logDir 'frontend')
  $owned.Add($process)
  Wait-ServiceReady $process {
    $response=Invoke-WebRequest $config.FrontendUrl -UseBasicParsing -TimeoutSec 3
    $owner=@(Get-NetTCPConnection -State Listen -LocalPort $config.Value.frontend.port -ErrorAction Stop | Where-Object { $_.OwningProcess -eq $process.Id })
    return ($response.StatusCode -eq 200 -and $owner.Count -gt 0)
  } $ReadyTimeoutSeconds 'Frontend'
  foreach ($entry in $owned) { if ($entry.HasExited) { throw 'A service exited while another service was starting.' } }
  & (Join-Path $PSScriptRoot 'check-health.ps1') -ConfigPath $config.Path -LaunchId $launch -ExpectedBuildId $build.build_id -ExpectedPythonDigest $pythonIdentity.source_digest
  Write-Utf8Json (Join-Path $logDir 'startup.json') @{launch_id=$launch; build_id=$build.build_id; configuration=$Configuration; config_path=$config.Path; executable=$exe; processes=@($owned | ForEach-Object { @{pid=$_.Id; started_at=$_.StartTime.ToUniversalTime().ToString('o');executable=$_.MainModule.FileName} })}
  Write-Host "All services ready. Frontend: $($config.FrontendUrl)"
  Write-Host "Launch: $launch; logs: $logDir"
  Write-Host ('Stop these services: scripts/dev/stop-all.ps1 -StartupRecord "{0}/startup.json"' -f $logDir)
  if ($OpenBrowser) { try { Start-Process $config.FrontendUrl } catch { Write-Warning 'Services are ready, but the browser could not be opened.' } }
  [pscustomobject]@{ LaunchId=$launch; LogDirectory=$logDir; Processes=$owned.ToArray() }
} catch {
  for ($index=$owned.Count-1; $index -ge 0; $index--) {
    try { Stop-OwnedService $owned[$index] } catch { Write-Warning 'A retained process could not be stopped; inspect the startup logs.' }
  }
  throw "$stage failed: $($_.Exception.Message) Logs: $logDir"
} finally { Restore-DevelopmentEnvironment $saved }
