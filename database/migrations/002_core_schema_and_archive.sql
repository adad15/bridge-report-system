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
do $$
begin
  if exists (
    select 1
    from information_schema.columns
    where table_schema = current_schema()
      and table_name = 'defect_photos'
      and column_name = 'match_status'
  ) then
    create index if not exists ix_defect_photos_observation_number_status
      on defect_photos (defect_observation_id, photo_number, match_status);
  end if;
end
$$;
create index if not exists ix_defect_threads_bridge_component_status on defect_threads (bridge_id, bridge_component_id, current_status);
create index if not exists ix_defect_comparisons_current_compared_status on defect_comparisons (current_inspection_year_id, compared_inspection_year_id, confirmation_status);
