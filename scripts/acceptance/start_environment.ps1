param([Parameter(Mandatory=$true)][string]$Manifest)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '../dev/common.ps1')
$state=Get-Content -LiteralPath $Manifest -Raw -Encoding UTF8 | ConvertFrom-Json
$config=Get-DevelopmentConfig $state.config_path
if ($config.Value.postgres.database -ne 'bridge_report_acceptance' -or $config.Value.postgres.port -ne $state.ports.postgres) {throw 'Acceptance database identity mismatch'}
foreach($part in @('backend','python','frontend')){Assert-PortAvailable '127.0.0.1' $state.ports.$part}
Assert-PreparedDependencies 'python'
Assert-PreparedDependencies 'frontend'
$root=Get-RepositoryRoot
$expectedRun=[IO.Path]::GetFullPath((Join-Path $root ('runtime/acceptance/'+$state.run_id)))
if([IO.Path]::GetFullPath($state.run_directory) -ne $expectedRun -or
 [IO.Path]::GetFullPath($state.config_path) -ne (Join-Path $expectedRun 'config.json')){throw 'Acceptance path mismatch'}
foreach($path in @($config.Value.archive.root,$config.Value.temporary_storage.root,$config.Value.temporary_storage.report_root)){
 if(-not [IO.Path]::GetFullPath($path).StartsWith($expectedRun+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw 'Storage is outside the acceptance run'}
}
$log=$state.log_directory
$savedEnvironment=@{}
foreach($name in @('BRIDGE_REPORT_TEST_DATABASE_URL','BRIDGE_REPORT_TEST_SCHEMA','BRIDGE_REPORT_ALLOW_LEGACY_FIXTURES','BRIDGE_REPORT_LAUNCH_ID','BRIDGE_TOOLS_HOST','BRIDGE_TOOLS_PORT')){$savedEnvironment[$name]=[Environment]::GetEnvironmentVariable($name)}
$owned=New-Object 'System.Collections.Generic.List[System.Diagnostics.Process]'
foreach($name in @('BRIDGE_REPORT_TEST_DATABASE_URL','BRIDGE_REPORT_TEST_SCHEMA','BRIDGE_REPORT_ALLOW_LEGACY_FIXTURES')){Remove-Item -LiteralPath "Env:$name" -ErrorAction SilentlyContinue}
$env:BRIDGE_REPORT_LAUNCH_ID=$state.run_id
$env:BRIDGE_TOOLS_HOST="127.0.0.1"
$env:BRIDGE_TOOLS_PORT=[string]$state.ports.python
try {
 $python=Join-Path $root 'tools-python/.venv/Scripts/python.exe'
 $process=Start-OwnedService $python @('-m','uvicorn','bridge_report_tools.main:app','--host','127.0.0.1','--port',[string]$state.ports.python,'--workers','1') (Join-Path $root 'tools-python') (Join-Path $log 'python')
 $owned.Add($process)
 Wait-ServiceReady $process { (Invoke-RestMethod "$($config.PythonUrl)/health" -TimeoutSec 3).status -eq 'ok' } 60 'Acceptance Python'
 $exe=Get-BackendArtifact 'backend-cpp/build/batch-a' 'Release'
 $process=Start-OwnedService $exe @($config.Path) $root (Join-Path $log 'backend')
 $owned.Add($process)
 Wait-ServiceReady $process { (Invoke-RestMethod "$($config.BackendUrl)/health/db" -TimeoutSec 3).status -eq 'ok' } 60 'Acceptance backend'
 $node=Find-DevelopmentTool 'node.exe'
 $process=Start-OwnedService $node @((Join-Path $PSScriptRoot 'serve-production.mjs'),(Join-Path $state.run_directory 'frontend-dist'),[string]$state.ports.frontend,$config.BackendUrl) $root (Join-Path $log 'frontend')
 $owned.Add($process)
 Wait-ServiceReady $process { (Invoke-WebRequest $config.FrontendUrl -UseBasicParsing -TimeoutSec 3).StatusCode -eq 200 } 30 'Acceptance frontend'
 $records=@($owned|ForEach-Object{@{pid=$_.Id;started_at=$_.StartTime.ToUniversalTime().ToString('o');executable=$_.MainModule.FileName}})
 Write-Utf8Json (Join-Path $log 'startup.json') @{launch_id=$state.run_id;processes=$records;config_path=$config.Path}
 $health=@{backend=(Invoke-RestMethod "$($config.BackendUrl)/health");database=(Invoke-RestMethod "$($config.BackendUrl)/health/db");python=(Invoke-RestMethod "$($config.PythonUrl)/health");tools=(Invoke-RestMethod "$($config.BackendUrl)/health/tools")}
 Write-Utf8Json (Join-Path $log 'health.json') $health
 Invoke-CheckedCommand $python @((Join-Path $PSScriptRoot 'verify_environment.py'),(Resolve-RepositoryPath $Manifest)) 'Acceptance isolation verification'
 Write-Host "Acceptance services ready: $($config.FrontendUrl)"
} catch {
 for($i=$owned.Count-1;$i -ge 0;$i--){Stop-OwnedService $owned[$i]}
 throw
} finally { Restore-DevelopmentEnvironment $savedEnvironment }
