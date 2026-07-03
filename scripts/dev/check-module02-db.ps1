$ErrorActionPreference = "Stop"

$databaseUrl = $env:BRIDGE_REPORT_DATABASE_URL
if ([string]::IsNullOrWhiteSpace($databaseUrl)) {
  $databaseUrl = "postgresql://bridge_report:bridge_report_dev@127.0.0.1:5432/bridge_report_system"
}

$psqlExe = $env:PSQL_EXE
if ([string]::IsNullOrWhiteSpace($psqlExe)) {
  $psqlExe = "psql"
}

& $psqlExe $databaseUrl -v ON_ERROR_STOP=1 -f database/migrations/002_core_schema_and_archive.sql
& $psqlExe $databaseUrl -v ON_ERROR_STOP=1 -f database/tests/002_core_schema_smoke.sql

Write-Host "Module 02 database check passed."
