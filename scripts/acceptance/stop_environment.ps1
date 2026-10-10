param([Parameter(Mandatory=$true)][string]$Manifest)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '../dev/common.ps1')
$state=Get-Content -LiteralPath $Manifest -Raw -Encoding UTF8 | ConvertFrom-Json
if($state.run_id -notmatch '^\d{8}-\d{6}-[a-f0-9]{6}$'){throw 'Invalid acceptance run id'}
$expected=[IO.Path]::GetFullPath((Join-Path (Get-RepositoryRoot) ('runtime/acceptance/'+$state.run_id)))
if([IO.Path]::GetFullPath($state.run_directory) -ne $expected){throw 'Run directory mismatch'}
$startup=Join-Path $state.log_directory 'startup.json'
if(Test-Path -LiteralPath $startup){
 $record=Get-Content -LiteralPath $startup -Raw -Encoding UTF8 | ConvertFrom-Json
 if($record.launch_id -ne $state.run_id){throw 'Startup identity mismatch'}
 & (Join-Path $PSScriptRoot '../dev/stop-all.ps1') -StartupRecord $startup
}
$data=Join-Path $expected 'postgres-data'
$pidFile=Join-Path $data 'postmaster.pid'
if(Test-Path -LiteralPath $pidFile){
 $lines=Get-Content -LiteralPath $pidFile
 if([IO.Path]::GetFullPath($lines[1]) -ne $data -or [int]$lines[3] -ne $state.ports.postgres){throw 'PostgreSQL identity mismatch'}
 Invoke-CheckedCommand (Join-Path $state.postgres_bin 'pg_ctl.exe') @('-D',$data,'-m','fast','-w','stop') 'Stop acceptance PostgreSQL'
}
$state.status='stopped'
Write-Utf8Json (Resolve-RepositoryPath $Manifest) $state
Write-Utf8Json (Join-Path $state.log_directory 'cleanup.json') @{status='stopped';stopped_at=(Get-Date).ToUniversalTime().ToString('o');data_retained=$true;run_directory=$expected}
Write-Host 'Acceptance processes stopped; database and evidence retained.'
