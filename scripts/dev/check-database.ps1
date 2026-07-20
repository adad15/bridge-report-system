$ErrorActionPreference = "Stop"

$databaseUrl = $env:BRIDGE_REPORT_DATABASE_URL
if ([string]::IsNullOrWhiteSpace($databaseUrl)) {
  $databaseUrl = "postgresql://bridge_report:bridge_report_dev@127.0.0.1:5432/bridge_report_system"
}

$psqlExe = $env:PSQL_EXE
if ([string]::IsNullOrWhiteSpace($psqlExe)) {
  $psqlCommand = Get-Command "psql" -ErrorAction SilentlyContinue
  if ($null -ne $psqlCommand) {
    $psqlExe = $psqlCommand.Source
  } else {
    $psqlCandidates = @(
      "D:\PostgreSQL\18\bin\psql.exe",
      "C:\Program Files\PostgreSQL\18\bin\psql.exe"
    )
    $psqlExe = $psqlCandidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
  }
}

if ([string]::IsNullOrWhiteSpace($psqlExe)) {
  throw "psql was not found; set PSQL_EXE to the PostgreSQL client path"
}

function Invoke-PsqlFile([string]$path) {
  & $psqlExe $databaseUrl -v ON_ERROR_STOP=1 -f $path
  if ($LASTEXITCODE -ne 0) {
    throw "psql failed for $path with exit code $LASTEXITCODE"
  }
}

$migrationFiles = Get-ChildItem "database/migrations/*.sql" | Sort-Object Name
foreach ($migrationFile in $migrationFiles) {
  Invoke-PsqlFile $migrationFile.FullName
}

# 新迁移必须可在同一数据库上安全复跑；第二遍在 smoke 前即时验证幂等性。
foreach ($migrationFile in $migrationFiles) {
  Invoke-PsqlFile $migrationFile.FullName
}

$smokeFiles = Get-ChildItem "database/tests/*.sql" | Sort-Object Name
foreach ($smokeFile in $smokeFiles) {
  Invoke-PsqlFile $smokeFile.FullName
}

Write-Host (
  "Full database migration and smoke check passed: {0} migrations applied twice; {1} smoke files passed." -f
    $migrationFiles.Count,
    $smokeFiles.Count
)
