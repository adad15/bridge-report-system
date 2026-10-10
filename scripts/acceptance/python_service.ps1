param([Parameter(Mandatory=$true)][string]$Manifest,[ValidateSet('Stop','Start')][string]$Action)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '../dev/common.ps1')
$state=Get-Content -LiteralPath $Manifest -Raw -Encoding UTF8 | ConvertFrom-Json
$recordPath=Join-Path $state.log_directory 'startup.json'
$record=Get-Content -LiteralPath $recordPath -Raw -Encoding UTF8 | ConvertFrom-Json
$python=Join-Path (Get-RepositoryRoot) 'tools-python/.venv/Scripts/python.exe'
if($record.launch_id -ne $state.run_id){throw 'Launch identity mismatch'}
$entry=@($record.processes|Where-Object {$_.executable -eq $python})[0]
if($Action -eq 'Stop'){
 $process=[Diagnostics.Process]::GetProcessById([int]$entry.pid)
 try{
  $null=$process.Handle
  $expected=if($entry.started_at -is [DateTime]){$entry.started_at.ToUniversalTime()}else{[DateTime]::Parse([string]$entry.started_at).ToUniversalTime()}
  if($process.StartTime.ToUniversalTime() -ne $expected -or $process.MainModule.FileName -ne $python){throw 'Process identity mismatch'}
  Stop-OwnedService $process
 }finally{$process.Dispose()}
}else{
 Assert-PortAvailable '127.0.0.1' $state.ports.python
 $env:BRIDGE_REPORT_LAUNCH_ID=$state.run_id
 $env:BRIDGE_TOOLS_HOST='127.0.0.1'
 $env:BRIDGE_TOOLS_PORT=[string]$state.ports.python
 $process=Start-OwnedService $python @('-m','uvicorn','bridge_report_tools.main:app','--host','127.0.0.1','--port',[string]$state.ports.python,'--workers','1') (Join-Path (Get-RepositoryRoot) 'tools-python') (Join-Path $state.log_directory ('python-restart-'+(Get-Date -Format 'HHmmss')))
 try{
  Wait-ServiceReady $process { (Invoke-RestMethod "http://127.0.0.1:$($state.ports.python)/health" -TimeoutSec 3).launch_id -eq $state.run_id } 45 'Acceptance Python restart'
  $record.processes=@($record.processes|Where-Object {$_.executable -ne $python})+@{pid=$process.Id;started_at=$process.StartTime.ToUniversalTime().ToString('o');executable=$process.MainModule.FileName}
  Write-Utf8Json $recordPath $record
 }catch{Stop-OwnedService $process;throw}
}
