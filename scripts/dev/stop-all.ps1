param([Parameter(Mandatory=$true)][string]$StartupRecord)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'common.ps1')
$record=Get-Content -LiteralPath (Resolve-RepositoryPath $StartupRecord) -Raw -Encoding UTF8 | ConvertFrom-Json
$entries=@($record.processes)
for ($index=$entries.Count-1; $index -ge 0; $index--) {
  $entry=$entries[$index]; $process=$null
  try {
    try { $process=[Diagnostics.Process]::GetProcessById([int]$entry.pid) } catch { continue }
    $null=$process.Handle
    $expected=if ($entry.started_at -is [DateTime]) { $entry.started_at.ToUniversalTime() } else { [DateTime]::Parse($entry.started_at).ToUniversalTime() }
    if ($process.StartTime.ToUniversalTime() -ne $expected -or -not $entry.executable -or
        $process.MainModule.FileName -ne $entry.executable) {
      Write-Warning "PID $($entry.pid) no longer matches the startup record; left untouched."
      continue
    }
    Stop-OwnedService $process
  } finally { if ($process) { $process.Dispose() } }
}
