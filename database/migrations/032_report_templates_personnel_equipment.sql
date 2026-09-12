-- 032：报告模板、人员库、设备库、年度报告配置与生成任务表。
-- 依赖 002（archived_files / inspection_years / users）、030；幂等可复跑。
--
-- 对应设计 2026-09-04 §7.1、§15、§17.1、§17.4。
--
-- 删除语义（§17.4）在这里只落数据库这一半，另一半必须同步改三个删除计划器和归档
-- 文件引用检查——那几处都是硬编码枚举依赖表的，只靠外键报错会让用户看到无法解释的
-- 失败，或者让预览的删除计数漏项。

-- ---------------------------------------------------------------------------
-- 模板
-- ---------------------------------------------------------------------------

create table if not exists report_templates (
  id uuid primary key default gen_random_uuid(),
  template_code text not null unique
    check (length(btrim(template_code)) > 0),
  template_name text not null
    check (length(btrim(template_name)) > 0),
  description text,
  contract_type text not null default 'periodic_inspection_v1'
    check (contract_type in ('periodic_inspection_v1')),
  -- 模板当前文件。RESTRICT：模板还在引用时不允许删除归档文件（§17.4）。
  file_id uuid not null references archived_files(id) on delete restrict,
  file_checksum text not null
    check (file_checksum ~ '^sha256:[0-9a-f]{64}$'),
  contract_config_json jsonb not null default '{}'::jsonb
    check (jsonb_typeof(contract_config_json) = 'object'),
  validation_status text not null default 'invalid'
    check (validation_status in ('valid', 'invalid')),
  validation_result_json jsonb not null default '{}'::jsonb
    check (jsonb_typeof(validation_result_json) = 'object'),
  is_enabled boolean not null default false,
  is_default boolean not null default false,
  updated_by_user_id uuid not null references users(id) on delete restrict,
  created_at timestamptz not null default now(),
  updated_at timestamptz not null default now(),
  -- 校验不通过的模板不许启用（§23.1：必须通过测试生成后才能启用）。
  constraint report_templates_enabled_requires_valid
    check (not is_enabled or validation_status = 'valid'),
  -- 默认模板必然是启用的，否则普通用户选不到它。
  constraint report_templates_default_requires_enabled
    check (not is_default or is_enabled)
);

-- 系统同时只能有一个默认模板（§7.1）。
create unique index if not exists ux_report_templates_default
  on report_templates (is_default)
  where is_default;

create index if not exists ix_report_templates_enabled
  on report_templates (is_enabled, template_name);

comment on column report_templates.contract_config_json is
  '模板配置：table_number_formats（键为 内容块 或 内容块:结构部位）与 '
  'required_personnel_roles。即 templates/report/<模板>/template.json 的内容。';

-- ---------------------------------------------------------------------------
-- 人员库与设备库
-- ---------------------------------------------------------------------------

create table if not exists report_personnel (
  id uuid primary key default gen_random_uuid(),
  full_name text not null check (length(btrim(full_name)) > 0),
  organization text,
  job_title text,
  professional_title text,
  qualification_certificate_no text,
  phone text,
  email text,
  remarks text,
  -- 被年度配置引用后不得硬删除，只能停用（§15.3）。
  is_enabled boolean not null default true,
  created_at timestamptz not null default now(),
  updated_at timestamptz not null default now()
);

create index if not exists ix_report_personnel_enabled
  on report_personnel (is_enabled, full_name);

create table if not exists report_equipment (
  id uuid primary key default gen_random_uuid(),
  equipment_name text not null check (length(btrim(equipment_name)) > 0),
  model_spec text,
  asset_number text,
  measurement_range text,
  accuracy text,
  calibration_certificate_no text,
  calibration_valid_until date,
  remarks text,
  is_enabled boolean not null default true,
  created_at timestamptz not null default now(),
  updated_at timestamptz not null default now()
);

create index if not exists ix_report_equipment_enabled
  on report_equipment (is_enabled, equipment_name);

-- ---------------------------------------------------------------------------
-- 年度报告配置
-- ---------------------------------------------------------------------------
--
-- 三张表都随所属年度级联删除，并且必须进入年度和桥梁的删除预览计数（§17.4）。
-- 对比检查的选择不在这里——它住在 inspection_years.report_comparison_inspection_id，
-- 不建第二份同义字段（§15.3）。

create table if not exists inspection_report_settings (
  id uuid primary key default gen_random_uuid(),
  inspection_year_id uuid not null unique
    references inspection_years(id) on delete cascade,
  -- 被年度配置引用的模板不许删除；停用模板不影响已保存的配置。
  template_id uuid references report_templates(id) on delete restrict,
  configured_by_user_id uuid references users(id) on delete restrict,
  configured_at timestamptz,
  created_at timestamptz not null default now(),
  updated_at timestamptz not null default now()
);

create table if not exists inspection_report_personnel (
  id uuid primary key default gen_random_uuid(),
  inspection_year_id uuid not null
    references inspection_years(id) on delete cascade,
  personnel_id uuid not null
    references report_personnel(id) on delete restrict,
  role_code text not null
    check (role_code in ('approver', 'reviewer', 'lead_inspector', 'compiler', 'participant')),
  sort_order integer not null default 0 check (sort_order >= 0),
  created_at timestamptz not null default now(),
  -- 同一个人可以在同一年度担任多个角色，但同一角色不能重复分配。
  unique (inspection_year_id, role_code, personnel_id)
);

create index if not exists ix_inspection_report_personnel_year
  on inspection_report_personnel (inspection_year_id, role_code, sort_order);

create table if not exists inspection_report_equipment (
  id uuid primary key default gen_random_uuid(),
  inspection_year_id uuid not null
    references inspection_years(id) on delete cascade,
  equipment_id uuid not null
    references report_equipment(id) on delete restrict,
  purpose text,
  sort_order integer not null default 0 check (sort_order >= 0),
  created_at timestamptz not null default now(),
  unique (inspection_year_id, equipment_id)
);

create index if not exists ix_inspection_report_equipment_year
  on inspection_report_equipment (inspection_year_id, sort_order);

-- ---------------------------------------------------------------------------
-- 生成任务
-- ---------------------------------------------------------------------------
--
-- 这是运行设施，不是报告版本表（§17.1）。系统不保存生成的报告，任务行只在到期前
-- 用于状态查询与下载，到期后清空一切可能含业务正文的字段。

create table if not exists report_generation_jobs (
  id uuid primary key default gen_random_uuid(),
  inspection_year_id uuid not null
    references inspection_years(id) on delete cascade,
  requested_by_user_id uuid not null references users(id) on delete restrict,
  status text not null default 'queued'
    check (status in (
      'queued', 'validating_data', 'assembling_docx', 'updating_fields',
      'validating_docx', 'ready', 'failed', 'expired'
    )),
  progress_json jsonb not null default '{}'::jsonb
    check (jsonb_typeof(progress_json) = 'object'),
  -- 创建任务时选择的模板。模板随后被替换或停用不影响本任务（§7.1、§17.3）。
  template_id uuid references report_templates(id) on delete set null,
  -- 仅用于本次任务诊断，不作为版本标识。
  template_checksum text,
  temporary_file_path text,
  error_code text,
  error_message text,
  created_at timestamptz not null default now(),
  finished_at timestamptz,
  expires_at timestamptz,
  -- 失败必须说明原因，否则界面只能显示"失败了"。
  constraint report_generation_jobs_failure_detail
    check (status <> 'failed' or error_code is not null),
  -- 到期后临时文件已删除，路径必须一并清空，避免留下指向不存在文件的记录（§17.1）。
  constraint report_generation_jobs_expired_has_no_file
    check (status <> 'expired' or temporary_file_path is null),
  -- 只有 ready 才可能提供下载，此时必须有文件。
  constraint report_generation_jobs_ready_has_file
    check (status <> 'ready' or temporary_file_path is not null)
);

-- 同一用户对同一年度只允许一个进行中的任务：重复提交返回已有任务而不是再起一个
-- （§17.3）。终态任务不受此限，可以反复重新生成。
create unique index if not exists ux_report_generation_jobs_active
  on report_generation_jobs (inspection_year_id, requested_by_user_id)
  where status in (
    'queued', 'validating_data', 'assembling_docx', 'updating_fields', 'validating_docx'
  );

create index if not exists ix_report_generation_jobs_year
  on report_generation_jobs (inspection_year_id, created_at desc);

-- 清理程序按到期时间扫，只看还没进入终态或仍持有文件的行。
create index if not exists ix_report_generation_jobs_expiry
  on report_generation_jobs (expires_at)
  where expires_at is not null;

comment on table report_generation_jobs is
  '报告生成任务，运行设施而非报告版本表。系统不保存生成的报告文件；'
  '到期后删除临时文件、状态转 expired 并清空路径与诊断正文，终态行保留 7 天后物理删除。';
