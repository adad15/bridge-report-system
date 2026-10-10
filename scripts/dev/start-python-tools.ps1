param([string]$ConfigPath)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'common.ps1')
$config=Get-DevelopmentConfig $ConfigPath
Assert-PreparedDependencies 'python'
Assert-PortAvailable $config.Value.python_tools.host $config.Value.python_tools.port
$oldHost=$env:BRIDGE_TOOLS_HOST; $oldPort=$env:BRIDGE_TOOLS_PORT
$env:BRIDGE_TOOLS_HOST=[string]$config.Value.python_tools.host
$env:BRIDGE_TOOLS_PORT=[string]$config.Value.python_tools.port
Push-Location (Join-Path (Get-RepositoryRoot) 'tools-python')
try {
  # Lock validation is done above. Run the prepared interpreter directly; no auto-install.
  Invoke-CheckedCommand (Join-Path (Get-Location) '.venv/Scripts/python.exe') @('-m','uvicorn','bridge_report_tools.main:app','--host',$env:BRIDGE_TOOLS_HOST,'--port',$env:BRIDGE_TOOLS_PORT,'--workers','1') 'Python service'
} finally { Pop-Location; $env:BRIDGE_TOOLS_HOST=$oldHost; $env:BRIDGE_TOOLS_PORT=$oldPort }
