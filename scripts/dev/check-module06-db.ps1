$ErrorActionPreference = "Stop"

$databaseUrl = $env:BRIDGE_REPORT_DATABASE_URL
if ([string]::IsNullOrWhiteSpace($databaseUrl)) {
  $databaseUrl = "postgresql://bridge_report:bridge_report_dev@127.0.0.1:5432/bridge_report_system"
}

$psqlExe = $env:PSQL_EXE
if ([string]::IsNullOrWhiteSpace($psqlExe)) {
  $psqlExe = "psql"
}

function Invoke-PsqlFile([string]$path) {
  & $psqlExe $databaseUrl -v ON_ERROR_STOP=1 -f $path
  if ($LASTEXITCODE -ne 0) {
    throw "psql failed for $path with exit code $LASTEXITCODE"
  }
}

Invoke-PsqlFile "database/migrations/002_core_schema_and_archive.sql"
Invoke-PsqlFile "database/migrations/003_component_rating_validation_and_thread_binding.sql"
Invoke-PsqlFile "database/migrations/004_users_and_import_reopen.sql"
Invoke-PsqlFile "database/migrations/005_import_record_edit_locks.sql"
Invoke-PsqlFile "database/migrations/006_inspection_year_deletion.sql"
Invoke-PsqlFile "database/migrations/007_bridge_administration.sql"
Invoke-PsqlFile "database/migrations/008_temporary_word_sources.sql"
Invoke-PsqlFile "database/migrations/009_import_record_deletion.sql"
Invoke-PsqlFile "database/tests/002_core_schema_smoke.sql"
Invoke-PsqlFile "database/tests/003_component_rating_archive_smoke.sql"
Invoke-PsqlFile "database/tests/006_inspection_year_deletion_smoke.sql"
Invoke-PsqlFile "database/tests/007_bridge_administration_smoke.sql"
Invoke-PsqlFile "database/tests/009_import_record_deletion_smoke.sql"

Write-Host "Module 06 database check passed."
