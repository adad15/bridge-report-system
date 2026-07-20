$ErrorActionPreference = "Stop"

# 解析仓库根目录：本脚本位于 scripts/dev/，仓库根是其上两级目录。
# 用 $PSScriptRoot 而不是依赖调用者的当前工作目录，这样不论从哪里调用，
# -f 传给 psql 的 SQL 文件路径都是可靠的。
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path

$databaseUrl = $env:BRIDGE_REPORT_DATABASE_URL
if ([string]::IsNullOrWhiteSpace($databaseUrl)) {
  $databaseUrl = "postgresql://bridge_report:bridge_report_dev@127.0.0.1:5432/bridge_report_system"
}

$psqlExe = $env:PSQL_EXE
if ([string]::IsNullOrWhiteSpace($psqlExe)) {
  $psqlExe = "psql"
}

# 样例数据含中文，强制客户端编码为 UTF8，避免本地代码页导致乱码或解析失败。
$env:PGCLIENTENCODING = "UTF8"

$sampleBridgeName = "绕阳河二号桥（模块05样例）"
$seedSqlPath = Join-Path $repoRoot "database/dev/seed_module05_review_sample.sql"
$sampleJsonPath = Join-Path $repoRoot "samples/contracts/bridge_annual_inspection_data.v2.valid.json"
$archiveRoot = $env:BRIDGE_REPORT_ARCHIVE_ROOT
if ([string]::IsNullOrWhiteSpace($archiveRoot)) {
  $archiveRoot = Join-Path $repoRoot "backend-cpp/archive"
}
$samplePhotoRelativePath = "photos/2.1-1.jpg"
$samplePhotoPath = Join-Path $archiveRoot $samplePhotoRelativePath

if (-not (Test-Path $seedSqlPath)) {
  throw "Seed SQL not found: $seedSqlPath"
}
if (-not (Test-Path $sampleJsonPath)) {
  throw "Sample contract JSON not found: $sampleJsonPath"
}

# 固定 JPEG：样例照片只用于验证受控内容接口和前端图片渲染，不冒充真实病害照片。
$samplePhotoDirectory = Split-Path -Parent $samplePhotoPath
New-Item -ItemType Directory -Force -Path $samplePhotoDirectory | Out-Null
Add-Type -AssemblyName System.Drawing
$bitmap = New-Object System.Drawing.Bitmap 16, 16
try {
  $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
  try { $graphics.Clear([System.Drawing.Color]::LightGray) } finally { $graphics.Dispose() }
  $bitmap.Save($samplePhotoPath, [System.Drawing.Imaging.ImageFormat]::Jpeg)
} finally { $bitmap.Dispose() }
$samplePhotoSize = (Get-Item -LiteralPath $samplePhotoPath).Length
$samplePhotoHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $samplePhotoPath).Hash.ToLowerInvariant()

Write-Host "Applying seed SQL (delete + reinsert sample bridge/year/import record)..."
& $psqlExe $databaseUrl -v ON_ERROR_STOP=1 -f $seedSqlPath
if ($LASTEXITCODE -ne 0) {
  throw "psql failed applying $seedSqlPath (exit code $LASTEXITCODE)"
}

# 把样例合同 JSON 灌入 parsed_result_json。
#
# 为什么这样做，而不是别的方式：
#   - psql 没有真正的绑定参数（bind parameter）。用 \set 变量替换去拼一段含单引号/
#     双引号/中文字符的大 JSON，在 Windows 本地代码页下很容易被截断或转义错误，
#     不可靠。
#   - pg_read_file() / lo_import 之类让服务端直接读文件的方式需要超级用户权限或
#     adminpack 扩展，本地开发库不一定具备，不能作为通用方案。
#   - 因此选择最直接可控的方案：PowerShell 把文件内容读成字符串，将其中的单引号
#     翻倍转义（SQL 标准的字符串转义规则），拼成一条 UPDATE 语句，连同后续的
#     SELECT 一起写入一个临时 .sql 文件，再用 -f 交给 psql 执行。整个过程内容都是
#     先在 PowerShell 侧构造好之后一次性写盘，不经过命令行参数长度限制，也不依赖
#     操作系统代码页去解析命令行参数。
#   - 临时文件写成不带 BOM 的 UTF-8：如果带 BOM，psql 会把 BOM 字节当成第一条语句
#     的一部分，导致解析报错。
$jsonContent = Get-Content -Raw -Encoding UTF8 $sampleJsonPath
$escapedJson = $jsonContent.Replace("'", "''")

$updateAndSelectSql = -join @(
  "update import_records", [Environment]::NewLine,
  "set parsed_result_json = '", $escapedJson, "'::jsonb", [Environment]::NewLine,
  "where bridge_id in (select id from bridges where bridge_name = '", $sampleBridgeName, "');", [Environment]::NewLine,
  [Environment]::NewLine,
  "with target as (", [Environment]::NewLine,
  "  select ir.id as import_record_id, ir.bridge_id, ir.inspection_year_id", [Environment]::NewLine,
  "  from import_records ir join bridges b on b.id = ir.bridge_id", [Environment]::NewLine,
  "  where b.bridge_name = '", $sampleBridgeName, "'", [Environment]::NewLine,
  "), sample_file as (", [Environment]::NewLine,
  "  insert into archived_files (bridge_id, inspection_year_id, original_file_name, current_file_name, storage_relative_path, file_type, file_purpose, file_extension, file_size_bytes, file_hash, source_description)", [Environment]::NewLine,
  "  select bridge_id, inspection_year_id, 'module05-sample.jpg', '2.1-1.jpg', '", $samplePhotoRelativePath, "', '图片', 'Word病害照片', '.jpg', ", $samplePhotoSize, ", '", $samplePhotoHash, "', '模块05确定性界面样例' from target returning id", [Environment]::NewLine,
  ")", [Environment]::NewLine,
  "insert into import_record_files (import_record_id, archived_file_id, file_role, process_status, process_note)", [Environment]::NewLine,
  "select target.import_record_id, sample_file.id, '附件', '处理成功', '照片候选：photo_0001' from target cross join sample_file;", [Environment]::NewLine,
  [Environment]::NewLine,
  "select id, system_number from import_records", [Environment]::NewLine,
  "where bridge_id in (select id from bridges where bridge_name = '", $sampleBridgeName, "');", [Environment]::NewLine
)

$tempSqlPath = Join-Path ([System.IO.Path]::GetTempPath()) ("seed_module05_review_sample_" + [Guid]::NewGuid().ToString("N") + ".sql")
try {
  $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
  [System.IO.File]::WriteAllText($tempSqlPath, $updateAndSelectSql, $utf8NoBom)

  Write-Host "Loading parsed_result_json from sample contract and printing import_record id..."
  & $psqlExe $databaseUrl -v ON_ERROR_STOP=1 -f $tempSqlPath
  if ($LASTEXITCODE -ne 0) {
    throw "psql failed applying parsed_result_json update (exit code $LASTEXITCODE)"
  }
} finally {
  if (Test-Path $tempSqlPath) {
    Remove-Item $tempSqlPath -Force
  }
}

Write-Host "Module 05 review sample seed applied."
