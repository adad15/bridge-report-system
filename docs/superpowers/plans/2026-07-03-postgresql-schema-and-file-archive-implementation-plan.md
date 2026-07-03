# PostgreSQL Schema and File Archive Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. This project currently uses Inline Execution, not Subagent-Driven execution.

**Goal:** Build module 02: the PostgreSQL core schema, file archive path rules, system number helpers, PostgreSQL config parsing, and a repeatable database smoke test for bridge yearly inspection facts.

**Architecture:** PostgreSQL owns structured facts through 14 core tables with UUID primary keys and stable Chinese business values. Files remain in the local archive root, while the database stores only metadata and archive-root-relative paths. C++ remains the future single fact-writing boundary; this module adds config and pure helpers, but does not implement Word parsing, import APIs, review pages, report templates, or report generation.

**Tech Stack:** PostgreSQL SQL migrations, PowerShell, C++20, Drogon existing core target, GTest, CMake, Visual Studio 2022.

---

## File Structure

Create and modify this structure:

```text
bridge-report-system/
  config/
    local.example.json
  database/
    migrations/
      002_core_schema_and_archive.sql
    tests/
      002_core_schema_smoke.sql
  backend-cpp/
    CMakeLists.txt
    include/
      bridge_report/
        archive/
          ArchivePaths.hpp
        config/
          AppConfig.hpp
        identity/
          SystemNumber.hpp
    src/
      archive/
        ArchivePaths.cpp
      config/
        AppConfig.cpp
      identity/
        SystemNumber.cpp
    tests/
      test_app_config.cpp
      test_archive_paths.cpp
      test_system_number.cpp
  scripts/
    dev/
      check-module02-db.ps1
```

Responsibility boundaries:

- `database/migrations/002_core_schema_and_archive.sql`: creates the module 02 PostgreSQL schema, sequences, constraints, and indexes.
- `database/tests/002_core_schema_smoke.sql`: runs a transactional insert/query/revision simulation and rolls back.
- `scripts/dev/check-module02-db.ps1`: applies the migration and runs the smoke test with `psql`.
- `backend-cpp/config`: parses PostgreSQL settings already present in `config/local.example.json`.
- `backend-cpp/identity`: formats and validates system numbers such as `QL-000001`.
- `backend-cpp/archive`: builds and validates archive-root-relative paths; it must never store or return a hard-coded absolute archive path for database persistence.

## Task 1: PostgreSQL Core Schema Migration

**Files:**

- Create: `database/migrations/002_core_schema_and_archive.sql`

- [ ] **Step 1: Run the migration before it exists to verify the missing implementation**

Run:

```powershell
psql "postgresql://bridge_report:bridge_report_dev@127.0.0.1:5432/bridge_report_system" -v ON_ERROR_STOP=1 -f database/migrations/002_core_schema_and_archive.sql
```

Expected: FAIL with a message that `database/migrations/002_core_schema_and_archive.sql` cannot be opened. If PostgreSQL is not running yet, start/create the local module database first and then rerun this step.

- [ ] **Step 2: Create the core schema migration**

Create `database/migrations/002_core_schema_and_archive.sql`:

```sql
create extension if not exists pgcrypto;

create sequence if not exists bridge_system_number_seq;
create sequence if not exists bridge_alias_system_number_seq;
create sequence if not exists inspection_year_system_number_seq;
create sequence if not exists archived_file_system_number_seq;
create sequence if not exists import_record_system_number_seq;
create sequence if not exists import_record_file_system_number_seq;
create sequence if not exists bridge_component_system_number_seq;
create sequence if not exists component_alias_system_number_seq;
create sequence if not exists defect_observation_system_number_seq;
create sequence if not exists defect_measurement_system_number_seq;
create sequence if not exists defect_photo_system_number_seq;
create sequence if not exists condition_rating_system_number_seq;
create sequence if not exists defect_thread_system_number_seq;
create sequence if not exists defect_comparison_system_number_seq;

create table if not exists bridges (
  id uuid primary key default gen_random_uuid(),
  system_number text not null unique default ('QL-' || lpad(nextval('bridge_system_number_seq')::text, 6, '0')),
  bridge_name text not null,
  business_code text,
  route_number text,
  route_name text,
  administrative_region text,
  station_mark text,
  longitude numeric(10, 7),
  latitude numeric(10, 7),
  bridge_type text,
  span_combination text,
  bridge_length_m numeric(10, 2),
  bridge_width_m numeric(10, 2),
  built_year integer check (built_year is null or built_year between 1800 and 2200),
  maintenance_org text,
  status text not null default '在用' check (status in ('在用', '停用', '拆除')),
  remarks text,
  created_at timestamptz not null default now(),
  updated_at timestamptz not null default now()
);

create table if not exists inspection_years (
  id uuid primary key default gen_random_uuid(),
  system_number text not null unique default ('NDJC-' || lpad(nextval('inspection_year_system_number_seq')::text, 6, '0')),
  bridge_id uuid not null references bridges(id) on delete restrict,
  inspection_year integer not null check (inspection_year between 1900 and 2200),
  inspection_date date,
  project_name text,
  inspection_org text,
  report_number text,
  version_number integer not null default 1 check (version_number >= 1),
  is_current boolean not null default true,
  revision_source_inspection_id uuid references inspection_years(id) on delete restrict,
  status text not null default '待校对' check (status in ('待校对', '已确认', '已被修订', '已归档')),
  previous_inspection_id uuid references inspection_years(id) on delete set null,
  overall_score numeric(6, 2),
  overall_grade text,
  created_at timestamptz not null default now(),
  updated_at timestamptz not null default now()
);

create table if not exists archived_files (
  id uuid primary key default gen_random_uuid(),
  system_number text not null unique default ('GDWJ-' || lpad(nextval('archived_file_system_number_seq')::text, 6, '0')),
  bridge_id uuid references bridges(id) on delete set null,
  inspection_year_id uuid references inspection_years(id) on delete set null,
  original_file_name text not null,
  current_file_name text not null,
  storage_relative_path text not null,
  file_type text not null check (file_type in ('Word文档', 'Excel表格', '图片', 'JSON快照', '生成报告', '模板', '附件')),
  file_purpose text not null,
  file_extension text,
  file_size_bytes bigint check (file_size_bytes is null or file_size_bytes >= 0),
  file_hash text,
  source_description text,
  uploaded_at timestamptz not null default now(),
  created_at timestamptz not null default now(),
  constraint archived_files_relative_path_check check (
    storage_relative_path !~ '^[A-Za-z]:[\\/]'
    and storage_relative_path !~ '^[\\/]'
    and storage_relative_path not like '%..%'
  )
);

create table if not exists bridge_aliases (
  id uuid primary key default gen_random_uuid(),
  system_number text not null unique default ('QLBM-' || lpad(nextval('bridge_alias_system_number_seq')::text, 6, '0')),
  bridge_id uuid not null references bridges(id) on delete cascade,
  alias_name text not null,
  source_type text not null default '人工录入' check (source_type in ('人工录入', '导入识别')),
  source_file_id uuid references archived_files(id) on delete set null,
  recognition_confidence numeric(5, 4) check (recognition_confidence is null or recognition_confidence between 0 and 1),
  is_manually_confirmed boolean not null default false,
  created_at timestamptz not null default now(),
  unique (bridge_id, alias_name)
);

create table if not exists import_records (
  id uuid primary key default gen_random_uuid(),
  system_number text not null unique default ('DRJL-' || lpad(nextval('import_record_system_number_seq')::text, 6, '0')),
  bridge_id uuid not null references bridges(id) on delete restrict,
  inspection_year_id uuid references inspection_years(id) on delete set null,
  import_name text not null,
  source_type text not null check (source_type in ('软件导出Word', '正式Word', 'Excel病害表', '图片包', '接口同步', 'JSON导入')),
  import_status text not null default '已上传' check (import_status in ('已上传', '解析中', '待校对', '已确认', '解析失败', '已取消')),
  main_file_id uuid references archived_files(id) on delete set null,
  importer_name text,
  importer_version text,
  parsed_result_json jsonb not null default '{}'::jsonb,
  validation_result_json jsonb not null default '{}'::jsonb,
  warning_summary text,
  error_message text,
  started_at timestamptz,
  finished_at timestamptz,
  created_at timestamptz not null default now(),
  updated_at timestamptz not null default now()
);

create table if not exists import_record_files (
  id uuid primary key default gen_random_uuid(),
  system_number text not null unique default ('DRWJ-' || lpad(nextval('import_record_file_system_number_seq')::text, 6, '0')),
  import_record_id uuid not null references import_records(id) on delete cascade,
  archived_file_id uuid not null references archived_files(id) on delete restrict,
  file_role text not null check (file_role in ('主报告', '病害表', '图片包', '附件', '接口快照')),
  process_status text not null default '待处理' check (process_status in ('待处理', '处理成功', '处理失败')),
  process_note text,
  created_at timestamptz not null default now(),
  unique (import_record_id, archived_file_id)
);

create table if not exists bridge_components (
  id uuid primary key default gen_random_uuid(),
  system_number text not null unique default ('GJ-' || lpad(nextval('bridge_component_system_number_seq')::text, 6, '0')),
  bridge_id uuid not null references bridges(id) on delete cascade,
  structure_part text not null check (structure_part in ('上部结构', '下部结构', '桥面系', '全桥', '其他')),
  component_type text not null,
  business_component_code text not null,
  span_number text,
  pier_abutment_number text,
  transverse_number text,
  longitudinal_number text,
  segment_number text,
  side text check (side is null or side in ('左侧', '右侧', '全幅')),
  location_description text,
  normalized_component_key text not null,
  current_status text not null default '待确认' check (current_status in ('待确认', '已确认', '停用')),
  creation_source text not null default '导入沉淀' check (creation_source in ('人工录入', '导入沉淀')),
  created_at timestamptz not null default now(),
  updated_at timestamptz not null default now(),
  unique (bridge_id, normalized_component_key)
);

create table if not exists component_aliases (
  id uuid primary key default gen_random_uuid(),
  system_number text not null unique default ('GJBM-' || lpad(nextval('component_alias_system_number_seq')::text, 6, '0')),
  bridge_component_id uuid not null references bridge_components(id) on delete cascade,
  alias_text text not null,
  source_file_id uuid references archived_files(id) on delete set null,
  recognition_confidence numeric(5, 4) check (recognition_confidence is null or recognition_confidence between 0 and 1),
  is_manually_confirmed boolean not null default false,
  created_at timestamptz not null default now(),
  unique (bridge_component_id, alias_text)
);

create table if not exists defect_threads (
  id uuid primary key default gen_random_uuid(),
  system_number text not null unique default ('BHXS-' || lpad(nextval('defect_thread_system_number_seq')::text, 6, '0')),
  bridge_id uuid not null references bridges(id) on delete cascade,
  bridge_component_id uuid not null references bridge_components(id) on delete restrict,
  thread_name text not null,
  defect_type text not null,
  defect_location text,
  first_seen_inspection_id uuid references inspection_years(id) on delete set null,
  latest_seen_inspection_id uuid references inspection_years(id) on delete set null,
  current_status text not null default '不确定' check (current_status in ('持续存在', '已修复', '未再发现', '不确定')),
  confirmation_status text not null default '待确认' check (confirmation_status in ('待确认', '人工已确认')),
  remarks text,
  created_at timestamptz not null default now(),
  updated_at timestamptz not null default now()
);

create table if not exists defect_observations (
  id uuid primary key default gen_random_uuid(),
  system_number text not null unique default ('BHGC-' || lpad(nextval('defect_observation_system_number_seq')::text, 6, '0')),
  inspection_year_id uuid not null references inspection_years(id) on delete cascade,
  bridge_id uuid not null references bridges(id) on delete cascade,
  bridge_component_id uuid not null references bridge_components(id) on delete restrict,
  defect_thread_id uuid references defect_threads(id) on delete set null,
  source_import_record_id uuid references import_records(id) on delete set null,
  source_file_id uuid references archived_files(id) on delete set null,
  source_page_number integer,
  source_table_title text,
  source_table_index integer,
  source_row_number integer,
  source_raw_cells_json jsonb not null default '{}'::jsonb,
  structure_part text not null check (structure_part in ('上部结构', '下部结构', '桥面系', '全桥', '其他')),
  part_name text,
  component_type text,
  business_component_code text,
  defect_location text,
  defect_type text not null,
  defect_description_raw text not null,
  scale text,
  defect_deduction numeric(8, 2),
  component_score numeric(8, 2),
  is_repaired text not null default '不确定' check (is_repaired in ('是', '否', '不确定')),
  extraction_confidence numeric(5, 4) check (extraction_confidence is null or extraction_confidence between 0 and 1),
  review_status text not null default '待校对' check (review_status in ('待校对', '已确认', '已修改', '已驳回')),
  review_note text,
  created_at timestamptz not null default now(),
  updated_at timestamptz not null default now()
);

create table if not exists defect_measurements (
  id uuid primary key default gen_random_uuid(),
  system_number text not null unique default ('BHCC-' || lpad(nextval('defect_measurement_system_number_seq')::text, 6, '0')),
  defect_observation_id uuid not null references defect_observations(id) on delete cascade,
  measurement_type text not null check (measurement_type in ('数量', '长度', '宽度', '最大宽度', '面积', '总面积', '间距', '尺寸组合', '未识别尺寸')),
  numeric_value numeric(12, 4),
  unit text,
  raw_text text not null,
  normalized_text text,
  is_auto_parsed boolean not null default true,
  is_manually_confirmed boolean not null default false,
  remarks text,
  created_at timestamptz not null default now()
);

create table if not exists defect_photos (
  id uuid primary key default gen_random_uuid(),
  system_number text not null unique default ('BHZP-' || lpad(nextval('defect_photo_system_number_seq')::text, 6, '0')),
  defect_observation_id uuid not null references defect_observations(id) on delete cascade,
  archived_file_id uuid references archived_files(id) on delete set null,
  source_import_record_id uuid references import_records(id) on delete set null,
  source_file_id uuid references archived_files(id) on delete set null,
  photo_number text not null,
  photo_title text,
  photo_description text,
  match_status text not null default '待校对' check (match_status in ('高置信候选', '待校对', '已确认', '未关联')),
  created_at timestamptz not null default now(),
  updated_at timestamptz not null default now()
);

create table if not exists condition_ratings (
  id uuid primary key default gen_random_uuid(),
  system_number text not null unique default ('JSPD-' || lpad(nextval('condition_rating_system_number_seq')::text, 6, '0')),
  inspection_year_id uuid not null references inspection_years(id) on delete cascade,
  source_import_record_id uuid references import_records(id) on delete set null,
  source_file_id uuid references archived_files(id) on delete set null,
  source_page_number integer,
  rating_level text not null check (rating_level in ('全桥', '结构分部', '部件', '构件', '项目')),
  structure_part text not null check (structure_part in ('全桥', '上部结构', '下部结构', '桥面系', '其他')),
  bridge_component_id uuid references bridge_components(id) on delete set null,
  rating_item_name text not null,
  score numeric(8, 2),
  grade text,
  weight numeric(8, 4),
  deduction numeric(8, 2),
  rating_text_raw text,
  review_status text not null default '待校对' check (review_status in ('待校对', '已确认', '已修改', '已驳回')),
  remarks text,
  created_at timestamptz not null default now(),
  updated_at timestamptz not null default now()
);

create table if not exists defect_comparisons (
  id uuid primary key default gen_random_uuid(),
  system_number text not null unique default ('BHDB-' || lpad(nextval('defect_comparison_system_number_seq')::text, 6, '0')),
  bridge_id uuid not null references bridges(id) on delete cascade,
  defect_thread_id uuid references defect_threads(id) on delete set null,
  current_inspection_year_id uuid not null references inspection_years(id) on delete cascade,
  compared_inspection_year_id uuid not null references inspection_years(id) on delete restrict,
  previous_defect_observation_id uuid references defect_observations(id) on delete set null,
  current_defect_observation_id uuid references defect_observations(id) on delete set null,
  comparison_result text not null check (comparison_result in ('延续', '新增', '消失', '已修复', '加重', '减轻', '基本无变化', '不确定')),
  change_summary text,
  suggested_confidence numeric(5, 4) check (suggested_confidence is null or suggested_confidence between 0 and 1),
  matching_evidence jsonb not null default '{}'::jsonb,
  confirmation_status text not null default '待确认' check (confirmation_status in ('待确认', '人工已确认', '已修改', '已驳回')),
  manual_note text,
  generated_at timestamptz not null default now(),
  confirmed_at timestamptz,
  created_at timestamptz not null default now(),
  updated_at timestamptz not null default now()
);

create unique index if not exists ux_inspection_years_current_bridge_year
  on inspection_years (bridge_id, inspection_year)
  where is_current;

create index if not exists ix_bridges_name_route on bridges (bridge_name, route_number, route_name);
create index if not exists ix_inspection_years_bridge_year_current on inspection_years (bridge_id, inspection_year, is_current);
create index if not exists ix_archived_files_bridge_year_type_hash on archived_files (bridge_id, inspection_year_id, file_type, file_hash);
create index if not exists ix_import_records_bridge_year_status on import_records (bridge_id, inspection_year_id, import_status);
create index if not exists ix_bridge_components_bridge_part_code on bridge_components (bridge_id, structure_part, business_component_code);
create index if not exists ix_defect_observations_year_component_type_review on defect_observations (inspection_year_id, bridge_component_id, defect_type, review_status);
create index if not exists ix_defect_photos_observation_number_status on defect_photos (defect_observation_id, photo_number, match_status);
create index if not exists ix_defect_threads_bridge_component_status on defect_threads (bridge_id, bridge_component_id, current_status);
create index if not exists ix_defect_comparisons_current_compared_status on defect_comparisons (current_inspection_year_id, compared_inspection_year_id, confirmation_status);
```

- [ ] **Step 3: Apply the migration**

Run:

```powershell
psql "postgresql://bridge_report:bridge_report_dev@127.0.0.1:5432/bridge_report_system" -v ON_ERROR_STOP=1 -f database/migrations/002_core_schema_and_archive.sql
```

Expected: PASS with `CREATE EXTENSION`, `CREATE SEQUENCE`, `CREATE TABLE`, and `CREATE INDEX` notices or `already exists` notices on reruns.

- [ ] **Step 4: Verify the 14 core tables exist**

Run:

```powershell
psql "postgresql://bridge_report:bridge_report_dev@127.0.0.1:5432/bridge_report_system" -v ON_ERROR_STOP=1 -c "select count(*) as core_table_count from information_schema.tables where table_schema = 'public' and table_name in ('bridges','bridge_aliases','inspection_years','archived_files','import_records','import_record_files','bridge_components','component_aliases','defect_observations','defect_measurements','defect_photos','condition_ratings','defect_threads','defect_comparisons');"
```

Expected:

```text
 core_table_count
------------------
               14
```

- [ ] **Step 5: Commit**

Run:

```powershell
git add database/migrations/002_core_schema_and_archive.sql
git commit -m "feat: add core PostgreSQL schema"
```

Expected: commit succeeds.

## Task 2: Database Smoke Test and Module Check Script

**Files:**

- Create: `database/tests/002_core_schema_smoke.sql`
- Create: `scripts/dev/check-module02-db.ps1`

- [ ] **Step 1: Write the failing module DB check script**

Create `scripts/dev/check-module02-db.ps1`:

```powershell
$ErrorActionPreference = "Stop"

$databaseUrl = $env:BRIDGE_REPORT_DATABASE_URL
if ([string]::IsNullOrWhiteSpace($databaseUrl)) {
  $databaseUrl = "postgresql://bridge_report:bridge_report_dev@127.0.0.1:5432/bridge_report_system"
}

psql $databaseUrl -v ON_ERROR_STOP=1 -f database/migrations/002_core_schema_and_archive.sql
psql $databaseUrl -v ON_ERROR_STOP=1 -f database/tests/002_core_schema_smoke.sql

Write-Host "Module 02 database check passed."
```

- [ ] **Step 2: Run the check script to verify it fails**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/dev/check-module02-db.ps1
```

Expected: FAIL because `database/tests/002_core_schema_smoke.sql` does not exist yet.

- [ ] **Step 3: Add the transactional smoke test**

Create `database/tests/002_core_schema_smoke.sql`:

```sql
begin;

do $$
declare
  v_bridge_id uuid;
  v_inspection_id uuid;
  v_revision_id uuid;
  v_source_file_id uuid;
  v_photo_file_id uuid;
  v_import_record_id uuid;
  v_component_id uuid;
  v_thread_id uuid;
  v_observation_id uuid;
  v_measurement_count integer;
  v_photo_count integer;
  v_rating_count integer;
  v_current_count integer;
  v_defect_rows integer;
begin
  insert into bridges (
    bridge_name,
    business_code,
    route_number,
    route_name,
    administrative_region,
    bridge_type,
    span_combination,
    bridge_length_m,
    bridge_width_m,
    maintenance_org
  )
  values (
    '绕阳河二号桥',
    'Q202605001-JZ-024',
    'S319',
    '辽小线',
    '辽宁省',
    '预应力混凝土空心板桥',
    '3x20m',
    66.00,
    12.00,
    '测试管养单位'
  )
  returning id into v_bridge_id;

  insert into bridge_aliases (
    bridge_id,
    alias_name,
    source_type,
    is_manually_confirmed
  )
  values (
    v_bridge_id,
    'S319辽小线绕阳河二号桥',
    '人工录入',
    true
  );

  insert into inspection_years (
    bridge_id,
    inspection_year,
    inspection_date,
    project_name,
    inspection_org,
    report_number,
    status,
    overall_score,
    overall_grade
  )
  values (
    v_bridge_id,
    2026,
    date '2026-06-20',
    '绕阳河二号桥 2026 定期检测',
    '测试检测单位',
    'Q202605001-JZ-024',
    '已确认',
    88.50,
    '2类'
  )
  returning id into v_inspection_id;

  begin
    insert into inspection_years (
      bridge_id,
      inspection_year,
      version_number,
      is_current,
      status
    )
    values (
      v_bridge_id,
      2026,
      99,
      true,
      '待校对'
    );

    raise exception 'Expected unique current inspection constraint to reject duplicate current year';
  exception
    when unique_violation then
      null;
  end;

  insert into archived_files (
    bridge_id,
    inspection_year_id,
    original_file_name,
    current_file_name,
    storage_relative_path,
    file_type,
    file_purpose,
    file_extension,
    file_size_bytes,
    file_hash,
    source_description
  )
  values (
    v_bridge_id,
    v_inspection_id,
    '绕阳河二号桥报告.docx',
    'GDWJ-000001_绕阳河二号桥报告.docx',
    'bridges/QL-000001_绕阳河二号桥/2026/imports/DRJL-000001_软件Word导入/input/GDWJ-000001_绕阳河二号桥报告.docx',
    'Word文档',
    '原始数据源',
    'docx',
    1024,
    'sha256:test-source',
    '用户上传'
  )
  returning id into v_source_file_id;

  insert into import_records (
    bridge_id,
    inspection_year_id,
    import_name,
    source_type,
    import_status,
    main_file_id,
    importer_name,
    importer_version,
    parsed_result_json,
    validation_result_json,
    warning_summary,
    started_at,
    finished_at
  )
  values (
    v_bridge_id,
    v_inspection_id,
    '绕阳河二号桥 2026 软件Word导入',
    '软件导出Word',
    '待校对',
    v_source_file_id,
    '软件Word报告导入器',
    '2026.07.03',
    '{"contract_version":"0.1","bridge_check":{"selected_bridge":"绕阳河二号桥"},"defects":[{"photo_number":"照片2.1-1"}]}'::jsonb,
    '{"photo_number_check":"高置信候选"}'::jsonb,
    '测试导入存在 1 条候选病害',
    now(),
    now()
  )
  returning id into v_import_record_id;

  insert into import_record_files (
    import_record_id,
    archived_file_id,
    file_role,
    process_status,
    process_note
  )
  values (
    v_import_record_id,
    v_source_file_id,
    '主报告',
    '处理成功',
    '主报告已归档'
  );

  insert into bridge_components (
    bridge_id,
    structure_part,
    component_type,
    business_component_code,
    span_number,
    transverse_number,
    side,
    location_description,
    normalized_component_key,
    current_status,
    creation_source
  )
  values (
    v_bridge_id,
    '上部结构',
    '空心板',
    '2-1#板',
    '第2孔',
    '1',
    '全幅',
    '第2跨第1片板',
    'upper|hollow-slab|2-1',
    '已确认',
    '导入沉淀'
  )
  returning id into v_component_id;

  insert into component_aliases (
    bridge_component_id,
    alias_text,
    source_file_id,
    recognition_confidence,
    is_manually_confirmed
  )
  values (
    v_component_id,
    '2-1号板',
    v_source_file_id,
    0.9300,
    true
  );

  insert into defect_threads (
    bridge_id,
    bridge_component_id,
    thread_name,
    defect_type,
    defect_location,
    first_seen_inspection_id,
    latest_seen_inspection_id,
    current_status,
    confirmation_status
  )
  values (
    v_bridge_id,
    v_component_id,
    '2-1#板底板横向裂缝',
    '横向裂缝',
    '底板',
    v_inspection_id,
    v_inspection_id,
    '持续存在',
    '人工已确认'
  )
  returning id into v_thread_id;

  insert into defect_observations (
    inspection_year_id,
    bridge_id,
    bridge_component_id,
    defect_thread_id,
    source_import_record_id,
    source_file_id,
    source_table_title,
    source_table_index,
    source_row_number,
    source_raw_cells_json,
    structure_part,
    part_name,
    component_type,
    business_component_code,
    defect_location,
    defect_type,
    defect_description_raw,
    scale,
    defect_deduction,
    component_score,
    is_repaired,
    extraction_confidence,
    review_status
  )
  values (
    v_inspection_id,
    v_bridge_id,
    v_component_id,
    v_thread_id,
    v_import_record_id,
    v_source_file_id,
    '上部结构病害检查表',
    2,
    1,
    '{"照片编号":"照片2.1-1","病害类型":"横向裂缝"}'::jsonb,
    '上部结构',
    '主梁',
    '空心板',
    '2-1#板',
    '底板',
    '横向裂缝',
    '底板横向裂缝 L=1.2m，W=0.15mm',
    '2',
    5.00,
    90.00,
    '否',
    0.9500,
    '已确认'
  )
  returning id into v_observation_id;

  insert into defect_measurements (
    defect_observation_id,
    measurement_type,
    numeric_value,
    unit,
    raw_text,
    normalized_text,
    is_auto_parsed,
    is_manually_confirmed
  )
  values
    (v_observation_id, '长度', 1.2000, 'm', 'L=1.2m', '长度=1.2m', true, true),
    (v_observation_id, '最大宽度', 0.1500, 'mm', 'W=0.15mm', '最大宽度=0.15mm', true, true);

  insert into archived_files (
    bridge_id,
    inspection_year_id,
    original_file_name,
    current_file_name,
    storage_relative_path,
    file_type,
    file_purpose,
    file_extension,
    file_size_bytes,
    file_hash,
    source_description
  )
  values (
    v_bridge_id,
    v_inspection_id,
    'image1.jpg',
    'GDWJ-000002_照片2.1-1.jpg',
    'bridges/QL-000001_绕阳河二号桥/2026/imports/DRJL-000001_软件Word导入/photos/GDWJ-000002_照片2.1-1.jpg',
    '图片',
    '病害照片',
    'jpg',
    2048,
    'sha256:test-photo',
    'Word内抽取'
  )
  returning id into v_photo_file_id;

  insert into defect_photos (
    defect_observation_id,
    archived_file_id,
    source_import_record_id,
    source_file_id,
    photo_number,
    photo_title,
    photo_description,
    match_status
  )
  values (
    v_observation_id,
    v_photo_file_id,
    v_import_record_id,
    v_source_file_id,
    '照片2.1-1',
    '2-1#板底板横向裂缝',
    '病害检查表照片编号与照片区标题一致',
    '已确认'
  );

  insert into condition_ratings (
    inspection_year_id,
    source_import_record_id,
    source_file_id,
    rating_level,
    structure_part,
    bridge_component_id,
    rating_item_name,
    score,
    grade,
    weight,
    deduction,
    rating_text_raw,
    review_status
  )
  values (
    v_inspection_id,
    v_import_record_id,
    v_source_file_id,
    '全桥',
    '全桥',
    null,
    '全桥技术状况',
    88.50,
    '2类',
    null,
    null,
    '全桥技术状况评定为2类。',
    '已确认'
  );

  update inspection_years
  set is_current = false,
      status = '已被修订',
      updated_at = now()
  where id = v_inspection_id;

  insert into inspection_years (
    bridge_id,
    inspection_year,
    version_number,
    is_current,
    revision_source_inspection_id,
    status,
    previous_inspection_id,
    report_number,
    overall_score,
    overall_grade
  )
  values (
    v_bridge_id,
    2026,
    2,
    true,
    v_inspection_id,
    '已确认',
    null,
    'Q202605001-JZ-024-REV2',
    89.00,
    '2类'
  )
  returning id into v_revision_id;

  insert into defect_comparisons (
    bridge_id,
    defect_thread_id,
    current_inspection_year_id,
    compared_inspection_year_id,
    previous_defect_observation_id,
    current_defect_observation_id,
    comparison_result,
    change_summary,
    suggested_confidence,
    matching_evidence,
    confirmation_status,
    confirmed_at
  )
  values (
    v_bridge_id,
    v_thread_id,
    v_revision_id,
    v_inspection_id,
    v_observation_id,
    null,
    '基本无变化',
    '修订版测试中保留原病害事实，实际对比由后续模块生成。',
    0.8000,
    '{"component":"2-1#板","defect_type":"横向裂缝"}'::jsonb,
    '人工已确认',
    now()
  );

  select count(*) into v_measurement_count
  from defect_measurements
  where defect_observation_id = v_observation_id;

  select count(*) into v_photo_count
  from defect_photos
  where defect_observation_id = v_observation_id;

  select count(*) into v_rating_count
  from condition_ratings
  where inspection_year_id = v_inspection_id;

  select count(*) into v_current_count
  from inspection_years
  where bridge_id = v_bridge_id
    and inspection_year = 2026
    and is_current = true;

  select count(*) into v_defect_rows
  from defect_observations o
  join defect_photos p on p.defect_observation_id = o.id
  join condition_ratings r on r.inspection_year_id = o.inspection_year_id
  where o.bridge_id = v_bridge_id
    and o.inspection_year_id = v_inspection_id
    and o.review_status = '已确认';

  if v_measurement_count <> 2 then
    raise exception 'Expected 2 measurements, got %', v_measurement_count;
  end if;

  if v_photo_count <> 1 then
    raise exception 'Expected 1 photo, got %', v_photo_count;
  end if;

  if v_rating_count <> 1 then
    raise exception 'Expected 1 condition rating, got %', v_rating_count;
  end if;

  if v_current_count <> 1 then
    raise exception 'Expected exactly 1 current inspection after revision, got %', v_current_count;
  end if;

  if v_defect_rows <> 1 then
    raise exception 'Expected query to find confirmed defect/photo/rating row, got %', v_defect_rows;
  end if;
end $$;

rollback;
```

- [ ] **Step 4: Run the module DB check**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/dev/check-module02-db.ps1
```

Expected:

```text
BEGIN
DO
ROLLBACK
Module 02 database check passed.
```

- [ ] **Step 5: Commit**

Run:

```powershell
git add database/tests/002_core_schema_smoke.sql scripts/dev/check-module02-db.ps1
git commit -m "test: add module 02 database smoke check"
```

Expected: commit succeeds.

## Task 3: PostgreSQL Config Parsing in C++ AppConfig

**Files:**

- Modify: `backend-cpp/include/bridge_report/config/AppConfig.hpp`
- Modify: `backend-cpp/src/config/AppConfig.cpp`
- Modify: `backend-cpp/tests/test_app_config.cpp`

- [ ] **Step 1: Write the failing config test**

Modify `backend-cpp/tests/test_app_config.cpp` and extend `LoadsConfiguredPortsAndArchiveRoot` with these expectations:

```cpp
    EXPECT_EQ(config.postgres.host, "127.0.0.1");
    EXPECT_EQ(config.postgres.port, 15432);
    EXPECT_EQ(config.postgres.database, "bridge_report_test");
    EXPECT_EQ(config.postgres.user, "bridge_report_tester");
    EXPECT_EQ(config.postgres.password, "secret");
```

Also update the JSON in `write_config_file()` so it contains:

```json
  "postgres": {
    "host": "127.0.0.1",
    "port": 15432,
    "database": "bridge_report_test",
    "user": "bridge_report_tester",
    "password": "secret"
  },
```

Extend `UsesDefaultsWhenConfigFileDoesNotExist` with:

```cpp
    EXPECT_EQ(config.postgres.host, "127.0.0.1");
    EXPECT_EQ(config.postgres.port, 5432);
    EXPECT_EQ(config.postgres.database, "bridge_report_system");
    EXPECT_EQ(config.postgres.user, "bridge_report");
    EXPECT_EQ(config.postgres.password, "bridge_report_dev");
```

- [ ] **Step 2: Run C++ tests to verify they fail**

Run:

```powershell
cd backend-cpp
ctest --preset vs2022-x64-debug
```

Expected: FAIL during compilation because `AppConfig` has no `postgres` member.

- [ ] **Step 3: Add PostgreSQL settings to `AppConfig.hpp`**

Modify `backend-cpp/include/bridge_report/config/AppConfig.hpp`:

```cpp
namespace bridge_report::config {

struct PostgresConfig {
    std::string host{"127.0.0.1"};
    int port{5432};
    std::string database{"bridge_report_system"};
    std::string user{"bridge_report"};
    std::string password{"bridge_report_dev"};
};

struct AppConfig {
    std::string host{"127.0.0.1"};
    int port{18080};
    std::string python_tools_base_url{"http://127.0.0.1:18081"};
    std::filesystem::path archive_root{"archive"};
    PostgresConfig postgres{};
};

AppConfig load_app_config(const std::filesystem::path& path);

}  // namespace bridge_report::config
```

- [ ] **Step 4: Parse PostgreSQL settings from JSON**

Modify `backend-cpp/src/config/AppConfig.cpp` inside `load_app_config()` after archive parsing:

```cpp
    const auto& postgres = root["postgres"];
    config.postgres.host = get_string_or_default(postgres, "host", config.postgres.host);
    config.postgres.port = get_int_or_default(postgres, "port", config.postgres.port);
    config.postgres.database = get_string_or_default(postgres, "database", config.postgres.database);
    config.postgres.user = get_string_or_default(postgres, "user", config.postgres.user);
    config.postgres.password = get_string_or_default(postgres, "password", config.postgres.password);
```

- [ ] **Step 5: Run C++ tests to verify they pass**

Run:

```powershell
cd backend-cpp
ctest --preset vs2022-x64-debug
```

Expected:

```text
100% tests passed
```

- [ ] **Step 6: Commit**

Run:

```powershell
git add backend-cpp/include/bridge_report/config/AppConfig.hpp backend-cpp/src/config/AppConfig.cpp backend-cpp/tests/test_app_config.cpp
git commit -m "feat: parse postgres app config"
```

Expected: commit succeeds.

## Task 4: System Number Helper

**Files:**

- Create: `backend-cpp/include/bridge_report/identity/SystemNumber.hpp`
- Create: `backend-cpp/src/identity/SystemNumber.cpp`
- Create: `backend-cpp/tests/test_system_number.cpp`
- Modify: `backend-cpp/CMakeLists.txt`

- [ ] **Step 1: Write the failing system number tests**

Create `backend-cpp/tests/test_system_number.cpp`:

```cpp
#include <stdexcept>

#include <gtest/gtest.h>

#include "bridge_report/identity/SystemNumber.hpp"

TEST(SystemNumberTest, FormatsSixDigitNumbers) {
    EXPECT_EQ(bridge_report::identity::format_system_number("QL", 1), "QL-000001");
    EXPECT_EQ(bridge_report::identity::format_system_number("NDJC", 42), "NDJC-000042");
    EXPECT_EQ(bridge_report::identity::format_system_number("BHDB", 123456), "BHDB-123456");
}

TEST(SystemNumberTest, RejectsInvalidSequenceValues) {
    EXPECT_THROW(bridge_report::identity::format_system_number("QL", 0), std::invalid_argument);
    EXPECT_THROW(bridge_report::identity::format_system_number("QL", -1), std::invalid_argument);
}

TEST(SystemNumberTest, KnowsModule02Prefixes) {
    EXPECT_TRUE(bridge_report::identity::is_supported_system_number_prefix("QL"));
    EXPECT_TRUE(bridge_report::identity::is_supported_system_number_prefix("QLBM"));
    EXPECT_TRUE(bridge_report::identity::is_supported_system_number_prefix("NDJC"));
    EXPECT_TRUE(bridge_report::identity::is_supported_system_number_prefix("GDWJ"));
    EXPECT_TRUE(bridge_report::identity::is_supported_system_number_prefix("DRJL"));
    EXPECT_TRUE(bridge_report::identity::is_supported_system_number_prefix("DRWJ"));
    EXPECT_TRUE(bridge_report::identity::is_supported_system_number_prefix("GJ"));
    EXPECT_TRUE(bridge_report::identity::is_supported_system_number_prefix("GJBM"));
    EXPECT_TRUE(bridge_report::identity::is_supported_system_number_prefix("BHGC"));
    EXPECT_TRUE(bridge_report::identity::is_supported_system_number_prefix("BHCC"));
    EXPECT_TRUE(bridge_report::identity::is_supported_system_number_prefix("BHZP"));
    EXPECT_TRUE(bridge_report::identity::is_supported_system_number_prefix("JSPD"));
    EXPECT_TRUE(bridge_report::identity::is_supported_system_number_prefix("BHXS"));
    EXPECT_TRUE(bridge_report::identity::is_supported_system_number_prefix("BHDB"));
    EXPECT_FALSE(bridge_report::identity::is_supported_system_number_prefix("REPORT"));
}
```

Modify `backend-cpp/CMakeLists.txt` and add `tests/test_system_number.cpp` to `bridge_report_backend_tests` before the helper exists.

- [ ] **Step 2: Run C++ tests to verify they fail**

Run:

```powershell
cd backend-cpp
cmake --build --preset vs2022-x64-debug
ctest --preset vs2022-x64-debug
```

Expected: FAIL because `bridge_report/identity/SystemNumber.hpp` does not exist.

- [ ] **Step 3: Add the system number header**

Create `backend-cpp/include/bridge_report/identity/SystemNumber.hpp`:

```cpp
#pragma once

#include <string>
#include <string_view>

namespace bridge_report::identity {

std::string format_system_number(std::string_view prefix, int sequence_value);
bool is_supported_system_number_prefix(std::string_view prefix);

}  // namespace bridge_report::identity
```

- [ ] **Step 4: Add the system number implementation**

Create `backend-cpp/src/identity/SystemNumber.cpp`:

```cpp
#include "bridge_report/identity/SystemNumber.hpp"

#include <array>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace bridge_report::identity {

namespace {

constexpr std::array<std::string_view, 14> kSupportedPrefixes{
    "QL",
    "QLBM",
    "NDJC",
    "GDWJ",
    "DRJL",
    "DRWJ",
    "GJ",
    "GJBM",
    "BHGC",
    "BHCC",
    "BHZP",
    "JSPD",
    "BHXS",
    "BHDB",
};

}  // namespace

std::string format_system_number(std::string_view prefix, int sequence_value) {
    if (sequence_value <= 0) {
        throw std::invalid_argument("System number sequence value must be positive.");
    }

    std::ostringstream output;
    output << prefix << "-" << std::setw(6) << std::setfill('0') << sequence_value;
    return output.str();
}

bool is_supported_system_number_prefix(std::string_view prefix) {
    for (const auto supported : kSupportedPrefixes) {
        if (supported == prefix) {
            return true;
        }
    }
    return false;
}

}  // namespace bridge_report::identity
```

- [ ] **Step 5: Wire the helper into CMake**

Modify `backend-cpp/CMakeLists.txt`:

```cmake
add_library(bridge_report_backend_core
    src/config/AppConfig.cpp
    src/http/Cors.cpp
    src/identity/SystemNumber.cpp
    src/runtime/RuntimePaths.cpp
)
```

Ensure the test executable includes:

```cmake
add_executable(bridge_report_backend_tests
    tests/test_app_config.cpp
    tests/test_system_number.cpp
)
```

- [ ] **Step 6: Run C++ tests to verify they pass**

Run:

```powershell
cd backend-cpp
cmake --build --preset vs2022-x64-debug
ctest --preset vs2022-x64-debug
```

Expected:

```text
100% tests passed
```

- [ ] **Step 7: Commit**

Run:

```powershell
git add backend-cpp/CMakeLists.txt backend-cpp/include/bridge_report/identity/SystemNumber.hpp backend-cpp/src/identity/SystemNumber.cpp backend-cpp/tests/test_system_number.cpp
git commit -m "feat: add system number helper"
```

Expected: commit succeeds.

## Task 5: Archive Relative Path Helper

**Files:**

- Create: `backend-cpp/include/bridge_report/archive/ArchivePaths.hpp`
- Create: `backend-cpp/src/archive/ArchivePaths.cpp`
- Create: `backend-cpp/tests/test_archive_paths.cpp`
- Modify: `backend-cpp/CMakeLists.txt`

- [ ] **Step 1: Write the failing archive path tests**

Create `backend-cpp/tests/test_archive_paths.cpp`:

```cpp
#include <filesystem>

#include <gtest/gtest.h>

#include "bridge_report/archive/ArchivePaths.hpp"

TEST(ArchivePathsTest, SanitizesUnsafePathParts) {
    EXPECT_EQ(
        bridge_report::archive::sanitize_path_part("DRJL-000001_软件/Word:导入*?"),
        "DRJL-000001_软件_Word_导入__"
    );
}

TEST(ArchivePathsTest, BuildsImportInputRelativePath) {
    const auto path = bridge_report::archive::build_import_input_relative_path(
        "QL-000001",
        "绕阳河二号桥",
        2026,
        "DRJL-000001",
        "软件Word导入",
        "GDWJ-000001",
        "绕阳河二号桥报告.docx"
    );

    EXPECT_EQ(
        path.generic_string(),
        "bridges/QL-000001_绕阳河二号桥/2026/imports/DRJL-000001_软件Word导入/input/GDWJ-000001_绕阳河二号桥报告.docx"
    );
}

TEST(ArchivePathsTest, BuildsExtractedPhotoRelativePath) {
    const auto path = bridge_report::archive::build_import_photo_relative_path(
        "QL-000001",
        "绕阳河二号桥",
        2026,
        "DRJL-000001",
        "软件Word导入",
        "GDWJ-000002",
        "照片2.1-1.jpg"
    );

    EXPECT_EQ(
        path.generic_string(),
        "bridges/QL-000001_绕阳河二号桥/2026/imports/DRJL-000001_软件Word导入/photos/GDWJ-000002_照片2.1-1.jpg"
    );
}

TEST(ArchivePathsTest, DetectsOnlySafeRelativeArchivePaths) {
    EXPECT_TRUE(bridge_report::archive::is_safe_archive_relative_path(
        std::filesystem::path("bridges/QL-000001/2026/imports/file.docx")
    ));
    EXPECT_FALSE(bridge_report::archive::is_safe_archive_relative_path(
        std::filesystem::path("D:/BridgeReportArchive/file.docx")
    ));
    EXPECT_FALSE(bridge_report::archive::is_safe_archive_relative_path(
        std::filesystem::path("/absolute/file.docx")
    ));
    EXPECT_FALSE(bridge_report::archive::is_safe_archive_relative_path(
        std::filesystem::path("../outside/file.docx")
    ));
}
```

Modify `backend-cpp/CMakeLists.txt` and add `tests/test_archive_paths.cpp` to `bridge_report_backend_tests` before the helper exists.

- [ ] **Step 2: Run C++ tests to verify they fail**

Run:

```powershell
cd backend-cpp
cmake --build --preset vs2022-x64-debug
ctest --preset vs2022-x64-debug
```

Expected: FAIL because `bridge_report/archive/ArchivePaths.hpp` does not exist.

- [ ] **Step 3: Add the archive path header**

Create `backend-cpp/include/bridge_report/archive/ArchivePaths.hpp`:

```cpp
#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace bridge_report::archive {

std::string sanitize_path_part(std::string_view value);

std::filesystem::path build_import_input_relative_path(
    std::string_view bridge_number,
    std::string_view bridge_name,
    int inspection_year,
    std::string_view import_number,
    std::string_view import_name,
    std::string_view file_number,
    std::string_view original_file_name
);

std::filesystem::path build_import_photo_relative_path(
    std::string_view bridge_number,
    std::string_view bridge_name,
    int inspection_year,
    std::string_view import_number,
    std::string_view import_name,
    std::string_view file_number,
    std::string_view photo_file_name
);

bool is_safe_archive_relative_path(const std::filesystem::path& path);

}  // namespace bridge_report::archive
```

- [ ] **Step 4: Add the archive path implementation**

Create `backend-cpp/src/archive/ArchivePaths.cpp`:

```cpp
#include "bridge_report/archive/ArchivePaths.hpp"

#include <cctype>
#include <sstream>

namespace bridge_report::archive {

namespace {

bool is_unsafe_path_char(char value) {
    switch (value) {
        case '<':
        case '>':
        case ':':
        case '"':
        case '/':
        case '\\':
        case '|':
        case '?':
        case '*':
            return true;
        default:
            return static_cast<unsigned char>(value) < 32;
    }
}

std::string numbered_name(std::string_view number, std::string_view name) {
    std::ostringstream output;
    output << sanitize_path_part(number) << "_" << sanitize_path_part(name);
    return output.str();
}

}  // namespace

std::string sanitize_path_part(std::string_view value) {
    std::string result;
    result.reserve(value.size());

    for (const char character : value) {
        result.push_back(is_unsafe_path_char(character) ? '_' : character);
    }

    while (!result.empty() && (result.back() == ' ' || result.back() == '.')) {
        result.pop_back();
    }

    if (result.empty()) {
        return "_";
    }

    return result;
}

std::filesystem::path build_import_input_relative_path(
    std::string_view bridge_number,
    std::string_view bridge_name,
    int inspection_year,
    std::string_view import_number,
    std::string_view import_name,
    std::string_view file_number,
    std::string_view original_file_name
) {
    return std::filesystem::path("bridges")
        / numbered_name(bridge_number, bridge_name)
        / std::to_string(inspection_year)
        / "imports"
        / numbered_name(import_number, import_name)
        / "input"
        / numbered_name(file_number, original_file_name);
}

std::filesystem::path build_import_photo_relative_path(
    std::string_view bridge_number,
    std::string_view bridge_name,
    int inspection_year,
    std::string_view import_number,
    std::string_view import_name,
    std::string_view file_number,
    std::string_view photo_file_name
) {
    return std::filesystem::path("bridges")
        / numbered_name(bridge_number, bridge_name)
        / std::to_string(inspection_year)
        / "imports"
        / numbered_name(import_number, import_name)
        / "photos"
        / numbered_name(file_number, photo_file_name);
}

bool is_safe_archive_relative_path(const std::filesystem::path& path) {
    if (path.empty() || path.is_absolute() || path.has_root_name() || path.has_root_directory()) {
        return false;
    }

    for (const auto& part : path) {
        if (part == "..") {
            return false;
        }
    }

    return true;
}

}  // namespace bridge_report::archive
```

- [ ] **Step 5: Wire the helper into CMake**

Modify `backend-cpp/CMakeLists.txt` so the core library includes:

```cmake
add_library(bridge_report_backend_core
    src/archive/ArchivePaths.cpp
    src/config/AppConfig.cpp
    src/http/Cors.cpp
    src/identity/SystemNumber.cpp
    src/runtime/RuntimePaths.cpp
)
```

Ensure the test executable includes:

```cmake
add_executable(bridge_report_backend_tests
    tests/test_app_config.cpp
    tests/test_archive_paths.cpp
    tests/test_system_number.cpp
)
```

- [ ] **Step 6: Run C++ tests to verify they pass**

Run:

```powershell
cd backend-cpp
cmake --build --preset vs2022-x64-debug
ctest --preset vs2022-x64-debug
```

Expected:

```text
100% tests passed
```

- [ ] **Step 7: Commit**

Run:

```powershell
git add backend-cpp/CMakeLists.txt backend-cpp/include/bridge_report/archive/ArchivePaths.hpp backend-cpp/src/archive/ArchivePaths.cpp backend-cpp/tests/test_archive_paths.cpp
git commit -m "feat: add archive relative path helper"
```

Expected: commit succeeds.

## Task 6: Module Verification and Documentation Touch-Up

**Files:**

- Modify: `README.md`
- Modify: `PROJECT_CONTEXT.md`

- [ ] **Step 1: Add module 02 verification notes to `README.md`**

Append this section to `README.md`:

````markdown
## Module 02 Database Check

Module 02 creates the PostgreSQL core schema and archive metadata foundation.

Set `BRIDGE_REPORT_DATABASE_URL` if your local database differs from the default:

```powershell
$env:BRIDGE_REPORT_DATABASE_URL = "postgresql://bridge_report:bridge_report_dev@127.0.0.1:5432/bridge_report_system"
```

Run:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/dev/check-module02-db.ps1
```

The check applies `database/migrations/002_core_schema_and_archive.sql` and runs the rollback-only smoke test in `database/tests/002_core_schema_smoke.sql`.
````

- [ ] **Step 2: Update `PROJECT_CONTEXT.md` current progress after implementation**

After module 02 is actually implemented and verified, update the current progress bullets to say:

```markdown
- 模块 2 `02-postgresql-schema-and-file-archive` 已完成实施：数据库迁移、系统编号工具、归档路径工具和数据库 smoke test 已通过。
- 下一步建议进入模块 3 `03-bridge-annual-inspection-data-contract`，先设计 C++ 与 Python 之间的年度检测数据 JSON 契约。
```

Do not remove existing module 2 design-document history. Keep the commit note `52ee0ab docs: add module 02 schema and archive design` if it is still useful context.

- [ ] **Step 3: Run all module 02 verification commands**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/dev/check-module02-db.ps1
cd backend-cpp
cmake --build --preset vs2022-x64-debug
ctest --preset vs2022-x64-debug
cd ..
```

Expected:

```text
Module 02 database check passed.
100% tests passed
```

- [ ] **Step 4: Commit**

Run:

```powershell
git add README.md PROJECT_CONTEXT.md
git commit -m "docs: document module 02 verification"
```

Expected: commit succeeds.

## Plan Self-Review

Spec coverage:

1. 14 core PostgreSQL tables: covered by Task 1 migration.
2. UUID internal keys and stable system numbers: covered by Task 1 SQL defaults and Task 4 C++ helper.
3. Bridge master data stays in database and Word does not auto-create bridges: covered by `import_records.bridge_id not null` and smoke test selecting an existing bridge first.
4. Candidate JSON stays in import records: covered by `import_records.parsed_result_json` and smoke test JSON insert.
5. Archive metadata and relative paths: covered by `archived_files.storage_relative_path` check constraint and Task 5 path helper.
6. Photo number matching storage: covered by `defect_photos.photo_number`, `match_status`, and smoke test.
7. Duplicate same-bridge same-year current inspection prevention: covered by partial unique index and smoke test duplicate-current assertion.
8. Revision version flow: covered by inspection version fields and smoke test changing v1 to `已被修订` and inserting v2 as current.
9. Simulated confirmation writes to formal tables: covered by smoke test inserts into components, defect observations, measurements, photos, ratings, threads, and comparisons.
10. Deferred tables remain deferred: this plan does not add maintenance records, section drafts, templates, or generated reports.

Red-flag scan:

1. The plan contains no unresolved markers such as TBD, TODO, or implement later.
2. Every file creation step includes exact file content.
3. Every verification step includes exact commands and expected outcomes.

Type and name consistency:

1. SQL table names are consistently English plural names.
2. Business status values remain Chinese strings as required by the module 02 design.
3. C++ namespace choices match the current project pattern: `bridge_report::config`, plus focused `bridge_report::identity` and `bridge_report::archive`.
4. The module DB script uses `BRIDGE_REPORT_DATABASE_URL` consistently.

## Execution Handoff

Plan complete and saved to `docs/superpowers/plans/2026-07-03-postgresql-schema-and-file-archive-implementation-plan.md`.

Execution mode for this project is already chosen:

**Inline Execution** - Execute tasks in this session using `superpowers:executing-plans`, with review checkpoints after each task and no subagent handoff.
