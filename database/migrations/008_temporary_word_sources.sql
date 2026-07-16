-- 008：Word 解析输入改为独立临时文件，不再作为正式 archived_files 长期保存。
-- 依赖 002_core_schema_and_archive.sql；幂等可复跑。

create sequence if not exists import_source_file_system_number_seq;

create table if not exists import_source_files (
  id uuid primary key default gen_random_uuid(),
  system_number text not null unique default (
    'LSWJ-' || lpad(nextval('import_source_file_system_number_seq')::text, 6, '0')
  ),
  import_record_id uuid unique references import_records(id) on delete set null,
  original_file_name text not null,
  storage_relative_path text not null unique,
  file_extension text not null default '.docx' check (lower(file_extension) = '.docx'),
  file_size_bytes bigint not null check (file_size_bytes >= 0),
  file_hash text not null,
  status text not null default '待解析' check (
    status in ('待解析', '解析中', '解析失败', '待清理', '清理中', '清理失败', '已删除', '已过期')
  ),
  cleanup_reason text check (cleanup_reason is null or cleanup_reason in ('解析成功', '已过期', '业务删除')),
  parsing_started_at timestamptz,
  expires_at timestamptz,
  last_error text,
  cleanup_attempt_count integer not null default 0 check (cleanup_attempt_count >= 0),
  next_cleanup_at timestamptz,
  created_at timestamptz not null default now(),
  updated_at timestamptz not null default now(),
  deleted_at timestamptz,
  constraint import_source_files_relative_path_check check (
    storage_relative_path ~ '^[0-9a-fA-F-]+[.]docx$'
    and storage_relative_path !~ '^[A-Za-z]:[\\/]'
    and storage_relative_path !~ '^[\\/]'
    and storage_relative_path not like '%..%'
  )
);

-- 允许年度或桥梁事务先删除业务记录，临时来源行继续充当可重试清理任务。
-- 下列重建约束同时让已应用过早期草案的本地数据库可幂等升级。
alter table import_source_files
  drop constraint if exists import_source_files_import_record_id_fkey;
alter table import_source_files
  alter column import_record_id drop not null;
alter table import_source_files
  add constraint import_source_files_import_record_id_fkey
  foreign key (import_record_id) references import_records(id) on delete set null;
alter table import_source_files
  drop constraint if exists import_source_files_cleanup_reason_check;
alter table import_source_files
  add constraint import_source_files_cleanup_reason_check check (
    cleanup_reason is null or cleanup_reason in ('解析成功', '已过期', '业务删除')
  );

create index if not exists ix_import_source_files_cleanup
  on import_source_files (status, expires_at, next_cleanup_at, updated_at)
  where status in ('解析中', '解析失败', '待清理', '清理失败');

comment on table import_source_files is
  '上传 Word 的临时解析输入；成功解析后立即清理，失败后按保留期清理。';
