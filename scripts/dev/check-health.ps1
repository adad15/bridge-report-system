param([string]$ConfigPath,[string]$LaunchId,[string]$ExpectedBuildId,[string]$ExpectedPythonDigest)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'common.ps1')
$config=Get-DevelopmentConfig $ConfigPath
$cpp=Invoke-RestMethod "$($config.BackendUrl)/health" -TimeoutSec 5
$python=Invoke-RestMethod "$($config.PythonUrl)/health" -TimeoutSec 5
if (-not (Test-ServiceIdentity $cpp 'bridge-report-cpp-backend' $LaunchId $ExpectedBuildId '')) { throw 'Backend identity is missing or does not match.' }
if (-not (Test-ServiceIdentity $python 'bridge-report-python-tools' $LaunchId '' $ExpectedPythonDigest)) { throw 'Python identity is missing or does not match.' }
$db=Invoke-RestMethod "$($config.BackendUrl)/health/db" -TimeoutSec 12
if ($db.status -ne 'ok' -or $db.database -ne 'reachable') { throw 'Backend database is not ready.' }
$via=Invoke-RestMethod "$($config.BackendUrl)/health/tools" -TimeoutSec 12
if ($via.tools_status -ne 'reachable') { throw 'Backend cannot reach Python.' }
if ($db.rating_tree_status -ne 'ok') { Write-Warning 'Database is reachable but rating-tree diagnostics are degraded; inspect /health/db.' }
Write-Host "Health verified: C++ $($cpp.build_id), Python $($python.source_digest)"
