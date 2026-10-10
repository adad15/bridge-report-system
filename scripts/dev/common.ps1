# Shared development helpers; Windows PowerShell 5.1 / PowerShell 7.
$script:RepositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
function Restore-DevelopmentEnvironment($Saved) {
  foreach ($name in $Saved.Keys) {
    if ($null -eq $Saved[$name]) { Remove-Item -LiteralPath "Env:$name" -ErrorAction SilentlyContinue }
    else { [Environment]::SetEnvironmentVariable($name,[string]$Saved[$name]) }
  }
}
function Get-RepositoryRoot { return $script:RepositoryRoot }
function Resolve-RepositoryPath([string]$Path) {
  if ([IO.Path]::IsPathRooted($Path)) { return [IO.Path]::GetFullPath($Path) }
  return [IO.Path]::GetFullPath((Join-Path (Get-RepositoryRoot) $Path))
}
function Get-DevelopmentConfig([string]$ConfigPath) {
  if ([string]::IsNullOrWhiteSpace($ConfigPath)) {
    $ConfigPath = 'config/local.json'
    if (-not (Test-Path -LiteralPath (Resolve-RepositoryPath $ConfigPath))) { $ConfigPath = 'config/local.example.json' }
  }
  $path = Resolve-RepositoryPath $ConfigPath
  if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Config file not found: $path" }
  try { $value = Get-Content -LiteralPath $path -Raw -Encoding UTF8 | ConvertFrom-Json }
  catch { throw "Config is not valid JSON: $path" }
  if ($value -isnot [pscustomobject]) { throw 'Config root must be a JSON object.' }
  $defaults = Get-Content -LiteralPath (Join-Path (Get-RepositoryRoot) 'config/local.example.json') -Raw -Encoding UTF8 | ConvertFrom-Json
  foreach ($section in $defaults.PSObject.Properties) {
    if ($null -eq $value.($section.Name)) { $value | Add-Member $section.Name $section.Value -Force; continue }
    if ($value.($section.Name) -isnot [pscustomobject]) { throw "Config section must be an object: $($section.Name)" }
    foreach ($field in $section.Value.PSObject.Properties) {
      if ($null -eq $value.($section.Name).($field.Name)) { $value.($section.Name) | Add-Member $field.Name $field.Value -Force }
    }
  }
  foreach ($section in @('cpp_server','python_tools','frontend')) {
    $endpoint = $value.$section
    if ($endpoint.port -isnot [int] -and $endpoint.port -isnot [long]) { throw "Invalid port in $section" }
    if ($endpoint.port -lt 1 -or $endpoint.port -gt 65535 -or ($endpoint.host -isnot [string]) -or [string]::IsNullOrWhiteSpace($endpoint.host)) { throw "Invalid endpoint in $section" }
  }
  # The application does not implement this variable. Do not probe a different DB.
  if ($env:BRIDGE_REPORT_DATABASE_URL) { throw 'BRIDGE_REPORT_DATABASE_URL is unsupported by the application launcher. Use ConfigPath.postgres; maintenance scripts have separate URL options.' }
  $pythonUrl = "http://$($value.python_tools.host):$($value.python_tools.port)"
  $backendUrl = "http://$($value.cpp_server.host):$($value.cpp_server.port)"
  if ($value.python_tools.base_url.TrimEnd('/') -ne $pythonUrl) { throw 'python_tools.base_url must match python_tools.host/port.' }
  if ($value.frontend.backend_base_url.TrimEnd('/') -ne $backendUrl) { throw 'frontend.backend_base_url must match cpp_server.host/port.' }
  return [pscustomobject]@{ Path=$path; Value=$value; BackendUrl=$backendUrl; PythonUrl=$pythonUrl; FrontendUrl="http://$($value.frontend.host):$($value.frontend.port)" }
}
function Find-DevelopmentTool([string]$Name, [string]$EnvironmentName) {
  if ($EnvironmentName) {
    $explicit = [Environment]::GetEnvironmentVariable($EnvironmentName)
    if ($explicit) {
      if (-not (Test-Path -LiteralPath $explicit -PathType Leaf)) { throw "$EnvironmentName does not identify a file." }
      return (Resolve-Path -LiteralPath $explicit).Path
    }
  }
  $command = Get-Command $Name -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
  if ($command) { return $command.Source }
  if ($Name -eq 'psql.exe') {
    foreach ($candidate in @('D:\PostgreSQL\18\bin\psql.exe','C:\Program Files\PostgreSQL\18\bin\psql.exe')) {
      if (Test-Path -LiteralPath $candidate) { return $candidate }
    }
  }
  throw "Required tool missing: $Name"
}
function Invoke-CheckedCommand([string]$Executable, [string[]]$Arguments, [string]$Stage='Command') {
  & $Executable @Arguments | Out-Host
  $result = $LASTEXITCODE
  if ($result -ne 0) { throw "$Stage failed (exit $result)." }
}
function Get-FileDigest([string]$Path) { return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() }
function Write-Utf8Json([string]$Path, $Value) {
  $parent = Split-Path -Parent $Path
  if ($parent) { [IO.Directory]::CreateDirectory($parent) | Out-Null }
  [IO.File]::WriteAllText($Path, ($Value | ConvertTo-Json -Depth 15), (New-Object Text.UTF8Encoding($false)))
}
function Get-DependencyFingerprint([string]$Component) {
  $root = Get-RepositoryRoot
  if ($Component -eq 'frontend') { return "$(Get-FileDigest (Join-Path $root 'frontend/package.json')):$(Get-FileDigest (Join-Path $root 'frontend/package-lock.json'))" }
  return "$(Get-FileDigest (Join-Path $root 'tools-python/pyproject.toml')):$(Get-FileDigest (Join-Path $root 'tools-python/uv.lock'))"
}
function Get-DependencyReceiptPath([string]$Component) {
  if ($Component -eq 'frontend') { return Join-Path (Get-RepositoryRoot) 'frontend/node_modules/.bridge-prepared.json' }
  return Join-Path (Get-RepositoryRoot) 'tools-python/.venv/.bridge-prepared.json'
}
function Assert-PreparedDependencies([string]$Component) {
  $receiptPath = Get-DependencyReceiptPath $Component
  if (-not (Test-Path -LiteralPath $receiptPath)) { throw "Run scripts/dev/prepare-dependencies.ps1 -Component $Component first." }
  $receipt = Get-Content -LiteralPath $receiptPath -Raw -Encoding UTF8 | ConvertFrom-Json
  if ($receipt.fingerprint -ne (Get-DependencyFingerprint $Component)) { throw "$Component dependency inputs changed; prepare dependencies again." }
  if ($Component -eq 'frontend') {
    $node = Find-DevelopmentTool 'node.exe'; $npm = Find-DevelopmentTool 'npm.cmd'
    $nodeVersion = (& $node --version).Trim()
    if ($LASTEXITCODE -ne 0) { throw 'Cannot read Node version.' }
    $npmVersion = (& $npm --version).Trim()
    if ($LASTEXITCODE -ne 0) { throw 'Cannot read npm version.' }
    if ($receipt.node -ne $nodeVersion -or $receipt.npm -ne $npmVersion) { throw 'Node/npm changed since preparation; prepare dependencies again.' }
    if (-not (Test-Path -LiteralPath (Join-Path (Get-RepositoryRoot) 'frontend/node_modules/vite/bin/vite.js'))) { throw 'Vite missing; prepare dependencies again.' }
  } else {
    if (-not (Test-Path -LiteralPath (Join-Path (Get-RepositoryRoot) 'tools-python/.venv/Scripts/python.exe'))) { throw 'Python environment missing.' }
    $uv = Find-DevelopmentTool 'uv.exe'
    $python = Join-Path (Get-RepositoryRoot) 'tools-python/.venv/Scripts/python.exe'
    $pythonVersion = (& $python --version).Trim()
    if ($LASTEXITCODE -ne 0) { throw 'Cannot read Python version.' }
    $uvVersion = (& $uv --version).Trim()
    if ($LASTEXITCODE -ne 0) { throw 'Cannot read uv version.' }
    if ($receipt.python -ne $pythonVersion -or $receipt.uv -ne $uvVersion) { throw 'Python/uv changed since preparation; prepare dependencies again.' }
    Push-Location (Join-Path (Get-RepositoryRoot) 'tools-python')
    try { Invoke-CheckedCommand $uv @('lock','--check') 'Python lock validation' } finally { Pop-Location }
  }
}
function Assert-PortAvailable([string]$ListenHost, [int]$Port) {
  $listeners = [Net.NetworkInformation.IPGlobalProperties]::GetIPGlobalProperties().GetActiveTcpListeners()
  if (@($listeners | Where-Object { $_.Port -eq $Port }).Count -gt 0) { throw "Port $Port is already in use. Stop the intended development instance explicitly." }
}
function Get-BackendArtifact([string]$BuildDirectory='backend-cpp/build/vs-debug', [string]$Configuration='Debug', [string]$Target='bridge-report-backend.exe') {
  return Join-Path (Join-Path (Resolve-RepositoryPath $BuildDirectory) $Configuration) $Target
}
function Assert-DatabaseReady($Config) {
  $psql = Find-DevelopmentTool 'psql.exe' 'PSQL_EXE'; $settings = $Config.Value.postgres
  $names = @('PGHOST','PGPORT','PGDATABASE','PGUSER','PGPASSWORD','PGCONNECT_TIMEOUT'); $saved = @{}
  foreach ($name in $names) { $saved[$name] = [Environment]::GetEnvironmentVariable($name) }
  try {
    $env:PGHOST=[string]$settings.host; $env:PGPORT=[string]$settings.port
    $env:PGDATABASE=[string]$settings.database; $env:PGUSER=[string]$settings.user
    $env:PGPASSWORD=[string]$settings.password; $env:PGCONNECT_TIMEOUT='5'
    $connectionArgs=@()
    if ($env:BRIDGE_REPORT_TEST_DATABASE_URL -or $env:BRIDGE_REPORT_TEST_SCHEMA) {
      if (-not $env:BRIDGE_REPORT_TEST_DATABASE_URL -or $env:BRIDGE_REPORT_TEST_SCHEMA -notmatch '^bridge_report_test[a-z0-9_]*$') { throw 'Invalid isolated test configuration.' }
      $connectionArgs=@('--dbname',$env:BRIDGE_REPORT_TEST_DATABASE_URL)
    }
    $oldPreference = $ErrorActionPreference; $ErrorActionPreference = 'Continue'
    try { & $psql @connectionArgs -X -w -v ON_ERROR_STOP=1 -tA -c 'select 1' 2>&1 | Out-Null; $code = $LASTEXITCODE } finally { $ErrorActionPreference = $oldPreference }
    if ($code -ne 0) { throw "Database check failed (exit $code). Check ConfigPath.postgres or isolated test settings." }
  } finally { Restore-DevelopmentEnvironment $saved }
}
function ConvertTo-NativeArgument([string]$Value) {
  return '"' + ([regex]::Replace([regex]::Replace($Value,'(\\*)"','$1$1\"'),'(\\+)$','$1$1')) + '"'
}
function Start-OwnedService([string]$Executable, [string[]]$Arguments, [string]$WorkingDirectory, [string]$LogPrefix) {
  $quoted = @($Arguments | ForEach-Object { ConvertTo-NativeArgument $_ })
  $process = Start-Process -FilePath $Executable -ArgumentList ($quoted -join ' ') -WorkingDirectory $WorkingDirectory -PassThru -WindowStyle Hidden -RedirectStandardOutput "$LogPrefix.stdout.log" -RedirectStandardError "$LogPrefix.stderr.log"
  $null = $process.Handle
  return $process
}
function Stop-RetainedProcess([Diagnostics.Process]$Process) {
  if ($Process.HasExited) { return }
  try { $Process.Kill() }
  catch {
    # Windows may return access denied when the child exits between HasExited and Kill.
    if (-not $Process.WaitForExit(1000)) { throw }
  }
  if (-not $Process.WaitForExit(5000)) { throw "Retained process $($Process.Id) did not exit." }
}
function Stop-OwnedService([Diagnostics.Process]$Process) {
  if ($null -eq $Process) { return }
  if ($Process.HasExited) {
    Write-Warning 'The retained parent has already exited; descendants cannot be safely inferred from its PID.'
    return
  }
  # Keep handles and verify creation times while the retained parent is still alive.
  # A bare PID or an executable name is never sufficient ownership evidence.
  $children = New-Object 'System.Collections.Generic.List[System.Diagnostics.Process]'
  $parents = New-Object 'System.Collections.Generic.Queue[System.Diagnostics.Process]'
  $parents.Enqueue($Process)
  try {
    while ($parents.Count -gt 0) {
      $parent = $parents.Dequeue()
      if ($parent.HasExited) { continue }
      foreach ($row in @(Get-CimInstance Win32_Process -Filter "ParentProcessId=$($parent.Id)" -ErrorAction Stop)) {
        $child = $null
        try {
          $child = [Diagnostics.Process]::GetProcessById([int]$row.ProcessId)
          $null = $child.Handle
          $born = $child.StartTime.ToUniversalTime()
          if ($parent.HasExited -or $born -lt $parent.StartTime.ToUniversalTime() -or
              [Math]::Abs(($born - $row.CreationDate.ToUniversalTime()).Ticks) -gt 9) {
            $child.Dispose(); continue
          }
          $children.Add($child)
          $parents.Enqueue($child)
        } catch { if ($child) { $child.Dispose() }; Write-Warning 'Could not confirm a descendant; it was left untouched.' }
      }
    }
    # Stop the root first so it cannot create replacement workers during cleanup.
    if (-not $Process.HasExited) {
      if ($Process.CloseMainWindow()) { $null = $Process.WaitForExit(2000) }
      if (-not $Process.HasExited) { Stop-RetainedProcess $Process }
    }
    for ($index=$children.Count-1; $index -ge 0; $index--) {
      $child=$children[$index]
      if (-not $child.HasExited) { Stop-RetainedProcess $child }
    }
  } finally { foreach ($child in $children) { $child.Dispose() } }
}
function Wait-ServiceReady([Diagnostics.Process]$Process, [scriptblock]$Probe, [int]$TimeoutSeconds=60, [string]$Service='Service') {
  $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
  do {
    if ($null -ne $Process -and $Process.HasExited) { throw "$Service exited before readiness (exit $($Process.ExitCode))." }
    try { if (& $Probe) { return } } catch { }
    Start-Sleep -Milliseconds 1000
  } while ([DateTime]::UtcNow -lt $deadline)
  throw "$Service did not become ready within $TimeoutSeconds seconds."
}
function Test-ServiceIdentity($Health,[string]$Service,[string]$LaunchId,[string]$BuildId,[string]$SourceDigest) {
  if ($Health.status -ne 'ok' -or $Health.service -ne $Service -or -not $Health.instance_id) { return $false }
  if ($LaunchId -and $Health.launch_id -ne $LaunchId) { return $false }
  if ($BuildId -and $Health.build_id -ne $BuildId) { return $false }
  if ($SourceDigest -and $Health.source_digest -ne $SourceDigest) { return $false }
  return $true
}
