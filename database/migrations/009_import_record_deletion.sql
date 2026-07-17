-- 009：管理员永久删除导入记录的审计与持久文件清理队列。
-- 依赖 002、004、005、007、008；幂等可复跑。

create sequence if not exists import_record_deletion_audit_system_number_seq;

create table if not exists import_record_deletion_audits (
  id uuid primary key default gen_random_uuid(),
  system_number text not null unique default (
    'DRSC-' || lpad(nextval('import_record_deletion_audit_system_number_seq')::text, 6, '0')
  ),
  original_import_record_id uuid not null,
  import_system_number_snapshot text not null,
  import_name_snapshot text not null,
  import_status_snapshot text not null,
  source_type_snapshot text not null,
  bridge_id uuid references bridges(id) on delete set null,
  bridge_system_number_snapshot text not null,
  bridge_name_snapshot text not null,
  inspection_year_id uuid references inspection_years(id) on delete set null,
  inspection_year_snapshot integer not null check (inspection_year_snapshot between 1900 and 2200),
  inspection_version_snapshot integer not null check (inspection_version_snapshot > 0),
  actor_user_id uuid references users(id) on delete set null,
  actor_username_snapshot text not null,
  actor_display_name_snapshot text not null,
  reason text not null check (length(btrim(reason)) between 1 and 1000),
  impact_json jsonb not null default '{}'::jsonb check (jsonb_typeof(impact_json) = 'object'),
  deleted_counts_json jsonb not null default '{}'::jsonb check (jsonb_typeof(deleted_counts_json) = 'object'),
  file_cleanup_status text not null default '待清理'
    check (file_cleanup_status in ('待清理', '已完成', '部分失败')),
  file_cleanup_last_error text,
  created_at timestamptz not null default now(),
  file_cleanup_completed_at timestamptz,
  constraint import_record_deletion_audits_cleanup_completion_check check (
    (file_cleanup_status = '已完成' and file_cleanup_completed_at is not null)
    or (file_cleanup_status <> '已完成' and file_cleanup_completed_at is null)
  )
);

create index if not exists ix_import_record_deletion_audits_original
  on import_record_deletion_audits (original_import_record_id, created_at desc);
create index if not exists ix_import_record_deletion_audits_bridge_year
  on import_record_deletion_audits (bridge_id, inspection_year_snapshot, created_at desc);
create index if not exists ix_import_record_deletion_audits_actor
  on import_record_deletion_audits (actor_user_id, created_at desc);

create table if not exists import_record_file_deletion_queue (
  id uuid primary key default gen_random_uuid(),
  import_record_deletion_audit_id uuid not null references import_record_deletion_audits(id) on delete cascade,
  storage_kind text not null check (storage_kind in ('归档存储', '临时Word存储')),
  artifact_kind text not null default '文件' check (artifact_kind in ('文件', '解析工作目录')),
  storage_relative_path text not null,
  status text not null default '待清理'
    check (status in ('待清理', '清理中', '已完成', '失败待重试')),
  attempt_count integer not null default 0 check (attempt_count >= 0),
  last_error text,
  next_attempt_at timestamptz not null default now(),
  processing_started_at timestamptz,
  created_at timestamptz not null default now(),
  completed_at timestamptz,
  unique (import_record_deletion_audit_id, storage_kind, artifact_kind, storage_relative_path),
  constraint import_record_file_deletion_queue_relative_path_check check (
    storage_relative_path !~ '^[A-Za-z]:[\\/]'
    and storage_relative_path !~ '^[\\/]'
    and storage_relative_path not like '%..%'
    and (
      (storage_kind = '临时Word存储' and artifact_kind = '文件'
       and storage_relative_path ~ '^[0-9a-fA-F-]+[.]docx$')
      or
      (storage_kind = '归档存储' and artifact_kind = '文件')
      or
      (storage_kind = '归档存储' and artifact_kind = '解析工作目录'
       and storage_relative_path ~ '^work/word-import/[0-9a-fA-F-]+-[0-9a-fA-F]+$')
    )
  ),
  constraint import_record_file_deletion_queue_completion_check check (
    (status = '已完成' and completed_at is not null and processing_started_at is null)
    or (status = '清理中' and completed_at is null and processing_started_at is not null)
    or (status in ('待清理', '失败待重试') and completed_at is null and processing_started_at is null)
  )
);

create index if not exists ix_import_record_file_deletion_queue_claimable
  on import_record_file_deletion_queue (next_attempt_at, created_at)
  where status in ('待清理', '失败待重试');
create index if not exists ix_import_record_file_deletion_queue_stale_processing
  on import_record_file_deletion_queue (processing_started_at)
  where status = '清理中';
create index if not exists ix_import_record_file_deletion_queue_audit_status
  on import_record_file_deletion_queue (import_record_deletion_audit_id, status);

alter table import_source_files
  add column if not exists active_parse_work_relative_path text;

alter table import_source_files
  drop constraint if exists import_source_files_active_parse_work_path_check;
alter table import_source_files
  add constraint import_source_files_active_parse_work_path_check check (
    active_parse_work_relative_path is null
    or (
      active_parse_work_relative_path ~ '^work/word-import/[0-9a-fA-F-]+-[0-9a-fA-F]+$'
      and active_parse_work_relative_path not like '%..%'
    )
  );

comment on table import_record_deletion_audits is
  '管理员永久删除未形成正式事实的导入记录后保留的不可恢复审计快照。';
comment on table import_record_file_deletion_queue is
  '导入记录数据库删除提交后，归档照片、临时 Word 和解析工作目录的持久清理任务。';
