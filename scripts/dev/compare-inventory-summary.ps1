# 构件台账聚合接口的搬迁验收：拿真实数据比对"旧的客户端汇总"与"新的服务端汇总 SQL"。
#
# 一次性脚本，不进 CI。直接用 psql 读库，不经后端，因此不需要登录态。
#
# 这条比对是必要不充分的：现网数据每构件只有 1 个生效映射、没有停用构件、
# sort_order 不重复，所以多映射计数放大、编号范围含停用构件、分组顺序平局这几类
# 问题在这份数据上根本不出现——一个把 count(*) 写错的实现也能全绿通过。那些边界
# 由 backend-cpp/tests/test_component_inventory_repository.cpp 的构造数据单测覆盖。
#
# 用法：
#   .\scripts\dev\compare-inventory-summary.ps1                  # 取构件最多的修订版
#   .\scripts\dev\compare-inventory-summary.ps1 -RevisionId <id> # 指定修订版

param(
  [string]$RevisionId = "",
  [string]$DatabaseUrl = ""
)

$ErrorActionPreference = "Stop"

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path

if ([string]::IsNullOrWhiteSpace($DatabaseUrl)) {
  $DatabaseUrl = $env:BRIDGE_REPORT_DATABASE_URL
}
if ([string]::IsNullOrWhiteSpace($DatabaseUrl)) {
  $DatabaseUrl = "postgresql://bridge_report:bridge_report_dev@127.0.0.1:5432/bridge_report_system"
}

$psqlExe = $env:PSQL_EXE
if ([string]::IsNullOrWhiteSpace($psqlExe)) {
  $psqlCommand = Get-Command "psql" -ErrorAction SilentlyContinue
  if ($null -ne $psqlCommand) {
    $psqlExe = $psqlCommand.Source
  } else {
    $psqlExe = @(
      "D:\PostgreSQL\18\bin\psql.exe",
      "C:\Program Files\PostgreSQL\18\bin\psql.exe"
    ) | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
  }
}
if ([string]::IsNullOrWhiteSpace($psqlExe)) {
  throw "psql was not found; set PSQL_EXE to the PostgreSQL client path"
}

function Invoke-Scalar([string]$sql) {
  $value = & $psqlExe $DatabaseUrl -v ON_ERROR_STOP=1 -At -c $sql
  if ($LASTEXITCODE -ne 0) { throw "psql failed: $sql" }
  return $value
}

if ([string]::IsNullOrWhiteSpace($RevisionId)) {
  $RevisionId = Invoke-Scalar (
    "select r.id::text from bridge_component_inventory_revisions r " +
    "join bridge_component_inventory_entries e on e.inventory_revision_id = r.id " +
    "group by r.id order by count(e.id) desc limit 1"
  )
  if ([string]::IsNullOrWhiteSpace($RevisionId)) {
    throw "库里没有任何带构件的台账修订版，无从比对"
  }
}
Write-Host "比对修订版：$RevisionId"

# 旧实现的输入：接口形状的 entries + 嵌套 mappings。字段名必须与
# frontend/src/api/componentInventoryApi.ts 的 ComponentInventoryEntry 一致，
# 否则比的就不是同一份数据。
$entriesSql = @"
select json_build_object(
  'id', r.id::text,
  'bridge_id', r.bridge_id::text,
  'revision_number', r.revision_number,
  'status', r.status,
  'baseline_revision_id', r.baseline_revision_id::text,
  'confirmed_at', r.confirmed_at::text,
  'entries', coalesce((
    select json_agg(json_build_object(
      'id', e.id::text,
      'bridge_component_id', e.bridge_component_id::text,
      'component_number', e.component_number,
      'site_name', e.site_name,
      'site_component_type', e.site_component_type,
      'span_or_location', e.span_or_location,
      'is_active', e.is_active,
      'deactivated_at', e.deactivated_at::text,
      'deactivation_reason', e.deactivation_reason,
      'sort_order', e.sort_order,
      'remarks', e.remarks,
      'is_referenced', false,
      'mappings', coalesce((
        select json_agg(json_build_object(
          'id', m.id::text,
          'standard_package_id', m.standard_package_id::text,
          'standard_bridge_type_id', m.standard_bridge_type_id,
          'standard_component_category_id', m.standard_component_category_id,
          'structure_part', m.structure_part,
          'mapping_source', m.mapping_source,
          'confirmation_status', m.confirmation_status,
          'is_active', m.is_active
        ) order by m.is_active desc, m.created_at, m.id)
        from bridge_component_standard_mappings m where m.inventory_entry_id = e.id
      ), '[]'::json)
    ) order by e.sort_order, e.id)
    from bridge_component_inventory_entries e where e.inventory_revision_id = r.id
  ), '[]'::json)
) from bridge_component_inventory_revisions r where r.id = '$RevisionId'::uuid
"@

$fixtureDir = Join-Path $env:TEMP "bridge-inventory-parity"
New-Item -ItemType Directory -Force -Path $fixtureDir | Out-Null
$fixturePath = Join-Path $fixtureDir "fixture.json"

$revisionJson = Invoke-Scalar $entriesSql
if ([string]::IsNullOrWhiteSpace($revisionJson)) { throw "修订版 $RevisionId 不存在" }

# 新实现的输出：调真正的 load_summary()，不在这里复制一份汇总 SQL——那正是本次
# 要消灭的分叉。测试二进制里有个惰性用例专门做这件事，两个环境变量都设了才会跑。
$testExe = @(
  (Join-Path $repositoryRoot "backend-cpp\build\vs-debug\Debug\bridge_report_backend_tests.exe"),
  (Join-Path $repositoryRoot "build\vs-debug\Debug\bridge_report_backend_tests.exe")
) | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if ([string]::IsNullOrWhiteSpace($testExe)) {
  throw "bridge_report_backend_tests.exe 未找到；先构建 C++ Debug 目标"
}

$summaryOut = Join-Path $fixtureDir "summary.json"
$previousPgOptions = $env:PGOPTIONS
try {
  # 不设 search_path：要读的是真库，不是隔离测试 schema。
  $env:PGOPTIONS = $null
  $env:INVENTORY_PARITY_REVISION = $RevisionId
  $env:INVENTORY_PARITY_OUT = $summaryOut
  & $testExe --gtest_filter="ComponentInventorySummaryDumpTest.*" | Out-Null
  if ($LASTEXITCODE -ne 0) { throw "导出新实现的汇总失败" }
} finally {
  $env:PGOPTIONS = $previousPgOptions
  $env:INVENTORY_PARITY_REVISION = $null
  $env:INVENTORY_PARITY_OUT = $null
}
$summaryJson = Get-Content -LiteralPath $summaryOut -Raw

Set-Content -LiteralPath $fixturePath -Encoding UTF8 -Value (
  "{""revision"":$revisionJson,""summary"":$summaryJson}"
)
Write-Host "夹具已导出：$fixturePath"

Push-Location (Join-Path $repositoryRoot "frontend")
try {
  $env:INVENTORY_PARITY_FIXTURE = $fixturePath
  & npx vitest run --reporter=verbose src/bridges/inventorySummaryParity.test.ts
  if ($LASTEXITCODE -ne 0) { throw "搬迁比对未通过：新旧汇总存在差异" }
  Write-Host "搬迁比对通过：新旧汇总逐字段一致"
} finally {
  Pop-Location
  $env:INVENTORY_PARITY_FIXTURE = $null
}
