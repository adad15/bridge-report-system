param([ValidateSet('all','frontend','python')][string]$Component='all',[string]$NpmRegistry)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'common.ps1')
foreach ($part in @('frontend','python')) {
  if ($Component -ne 'all' -and $Component -ne $part) { continue }
  $before=Get-DependencyFingerprint $part
  # A failed refresh must not leave a previous successful receipt usable.
  $receiptPath=Get-DependencyReceiptPath $part
  if (Test-Path -LiteralPath $receiptPath) { Remove-Item -LiteralPath $receiptPath -Force }
  if ($part -eq 'frontend') {
    $npm=Find-DevelopmentTool 'npm.cmd'; $node=Find-DevelopmentTool 'node.exe'
    $nodeVersion=(& $node --version).Trim()
    if ($LASTEXITCODE -ne 0) { throw 'Cannot determine Node version.' }
    $npmVersion=(& $npm --version).Trim()
    if ($LASTEXITCODE -ne 0) { throw 'Cannot determine npm version.' }
    $tools=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'toolchain.json') -Raw | ConvertFrom-Json
    if ($nodeVersion -ne $tools.node -or $npmVersion -ne $tools.npm) { throw "Validated frontend toolchain: Node $($tools.node), npm $($tools.npm). Validate a different toolchain explicitly before updating toolchain.json." }
    Push-Location (Join-Path (Get-RepositoryRoot) 'frontend')
    try {
      $arguments=@('ci')
      if ($NpmRegistry) { $arguments += @('--registry',$NpmRegistry) }
      Invoke-CheckedCommand $npm $arguments 'Locked frontend installation'
    } finally { Pop-Location }
    $receipt=@{fingerprint=$before; node=$nodeVersion; npm=$npmVersion; prepared_at=[DateTime]::UtcNow.ToString('o')}
  } else {
    $uv=Find-DevelopmentTool 'uv.exe'
    $tools=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'toolchain.json') -Raw | ConvertFrom-Json
    $uvVersion=(& $uv --version).Trim()
    if ($LASTEXITCODE -ne 0 -or ($uvVersion -split ' ')[1] -ne $tools.uv) { throw "Validated uv version is $($tools.uv)." }
    Push-Location (Join-Path (Get-RepositoryRoot) 'tools-python')
    try {
      Invoke-CheckedCommand $uv @('sync','--locked','--extra','dev','--python',[string]$tools.python) 'Locked Python installation'
      $python=Join-Path (Get-Location) '.venv/Scripts/python.exe'
      $pythonVersion=(& $python --version).Trim()
      if ($LASTEXITCODE -ne 0) { throw 'Prepared interpreter unavailable.' }
      $uvVersion=(& $uv --version).Trim()
      if ($LASTEXITCODE -ne 0) { throw 'Cannot determine uv version.' }
    } finally { Pop-Location }
    $receipt=@{fingerprint=$before; python=$pythonVersion; uv=$uvVersion; prepared_at=[DateTime]::UtcNow.ToString('o')}
  }
  if ((Get-DependencyFingerprint $part) -ne $before) { throw "$part installation changed its input files." }
  Write-Utf8Json (Get-DependencyReceiptPath $part) $receipt
  Write-Host "$part dependencies prepared from locked inputs."
}
