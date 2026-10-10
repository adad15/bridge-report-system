param([string]$ConfigPath)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'common.ps1')
$config=Get-DevelopmentConfig $ConfigPath
Assert-PreparedDependencies 'frontend'
Assert-PortAvailable $config.Value.frontend.host $config.Value.frontend.port
$previous=$env:VITE_BACKEND_BASE_URL
$env:VITE_BACKEND_BASE_URL=$config.BackendUrl
Push-Location (Join-Path (Get-RepositoryRoot) 'frontend')
try {
  Invoke-CheckedCommand (Find-DevelopmentTool 'node.exe') @('node_modules/vite/bin/vite.js','--host',[string]$config.Value.frontend.host,'--port',[string]$config.Value.frontend.port,'--strictPort') 'Frontend service'
} finally { Pop-Location; $env:VITE_BACKEND_BASE_URL=$previous }
