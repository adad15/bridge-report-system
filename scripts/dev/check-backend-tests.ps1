$ErrorActionPreference = "Stop"

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$databaseUrl = $env:BRIDGE_REPORT_TEST_DATABASE_URL
if ([string]::IsNullOrWhiteSpace($databaseUrl)) {
  $databaseUrl = "postgresql://bridge_report:bridge_report_dev@127.0.0.1:5432/bridge_report_system"
}

$testSchema = $env:BRIDGE_REPORT_TEST_SCHEMA
if ([string]::IsNullOrWhiteSpace($testSchema)) {
  $testSchema = "bridge_report_test"
}
if ($testSchema -notmatch '^bridge_report_test[a-z0-9_]*$') {
  throw "Refusing to reset unsafe test schema '$testSchema'; expected bridge_report_test*"
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

$testExeCandidates = @(
  (Join-Path $repositoryRoot "build\vs-debug\Debug\bridge_report_backend_tests.exe"),
  (Join-Path $repositoryRoot "backend-cpp\build\vs-debug\Debug\bridge_report_backend_tests.exe")
)
$testExe = $testExeCandidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if ([string]::IsNullOrWhiteSpace($testExe)) {
  throw "bridge_report_backend_tests.exe was not found; build the C++ Debug target first"
}

function Invoke-PsqlFile([string]$path) {
  & $psqlExe $databaseUrl -v ON_ERROR_STOP=1 -f $path
  if ($LASTEXITCODE -ne 0) {
    throw "psql failed for $path with exit code $LASTEXITCODE"
  }
}

$previousPgOptions = $env:PGOPTIONS
$previousTestUrl = $env:BRIDGE_REPORT_TEST_DATABASE_URL
$previousTestSchema = $env:BRIDGE_REPORT_TEST_SCHEMA

Push-Location $repositoryRoot
try {
  # schema 名称已经过严格白名单校验；每次从空 schema 开始，避免夹具残留影响后续运行。
  & $psqlExe $databaseUrl -v ON_ERROR_STOP=1 -c (
    "drop schema if exists $testSchema cascade; create schema $testSchema authorization bridge_report;"
  )
  if ($LASTEXITCODE -ne 0) {
    throw "failed to reset isolated test schema '$testSchema'"
  }

  $env:PGOPTIONS = "-c search_path=$testSchema"
  $migrationFiles = Get-ChildItem "database/migrations/*.sql" | Sort-Object Name
  foreach ($migrationFile in $migrationFiles) {
    Invoke-PsqlFile $migrationFile.FullName
  }
  foreach ($migrationFile in $migrationFiles) {
    Invoke-PsqlFile $migrationFile.FullName
  }

  $smokeFiles = Get-ChildItem "database/tests/*.sql" | Sort-Object Name
  foreach ($smokeFile in $smokeFiles) {
    Invoke-PsqlFile $smokeFile.FullName
  }

  # 仓储集成测试通过默认管理员 ID 构造删除审计；测试 schema 不启动后端，需显式播种。
  & $psqlExe $databaseUrl -v ON_ERROR_STOP=1 -c (
    "insert into users(username,display_name,password_hash,role) values " +
    "('admin','Test Administrator','not-a-real-hash','admin')," +
    "('user','Test User','not-a-real-hash','normal') " +
    "on conflict(username) do nothing;"
  )
  if ($LASTEXITCODE -ne 0) {
    throw "failed to seed default users in isolated test schema '$testSchema'"
  }

  $env:BRIDGE_REPORT_TEST_DATABASE_URL = $databaseUrl
  $env:BRIDGE_REPORT_TEST_SCHEMA = $testSchema
  & $testExe --gtest_brief=1
  if ($LASTEXITCODE -ne 0) {
    throw "C++ backend tests failed with exit code $LASTEXITCODE"
  }

  # 成功后删除整个隔离 schema，确保测试数据不会在本机继续累积。
  $env:PGOPTIONS = $previousPgOptions
  & $psqlExe $databaseUrl -v ON_ERROR_STOP=1 -c "drop schema $testSchema cascade;"
  if ($LASTEXITCODE -ne 0) {
    throw "backend tests passed, but isolated schema '$testSchema' could not be removed"
  }

  Write-Host (
    "Isolated backend check passed and schema {0} was removed: {1} migrations applied twice; {2} smoke files passed." -f
      $testSchema,
      $migrationFiles.Count,
      $smokeFiles.Count
  )
} finally {
  Pop-Location
  $env:PGOPTIONS = $previousPgOptions
  $env:BRIDGE_REPORT_TEST_DATABASE_URL = $previousTestUrl
  $env:BRIDGE_REPORT_TEST_SCHEMA = $previousTestSchema
}
