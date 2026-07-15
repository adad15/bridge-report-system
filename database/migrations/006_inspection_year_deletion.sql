-- 006：管理员永久删除年度检测的审计与归档文件清理队列
-- 依赖 002、004；幂等可复跑。

create sequence if not exists inspection_year_deletion_audit_system_number_seq;

create table if not exists inspection_year_deletion_audits (
  id uuid primary key default gen_random_uuid(),
  system_number text not null unique default (
    'NDSC-' || lpad(nextval('inspection_year_deletion_audit_system_number_seq')::text, 6, '0')
  ),
  bridge_id uuid references bridges(id) on delete set null,
  bridge_system_number_snapshot text not null,
  bridge_name_snapshot text not null,
  inspection_year integer not null check (inspection_year between 1900 and 2200),
  actor_user_id uuid references users(id) on delete set null,
  actor_username_snapshot text not null,
  actor_display_name_snapshot text not null,
  reason text not null check (length(btrim(reason)) > 0),
  impact_json jsonb not null default '{}'::jsonb check (jsonb_typeof(impact_json) = 'object'),
  deleted_counts_json jsonb not null default '{}'::jsonb check (jsonb_typeof(deleted_counts_json) = 'object'),
  file_cleanup_status text not null default '待清理'
    check (file_cleanup_status in ('待清理', '已完成', '部分失败')),
  created_at timestamptz not null default now(),
  file_cleanup_completed_at timestamptz
);

create index if not exists ix_inspection_year_deletion_audits_bridge_year_time
  on inspection_year_deletion_audits (bridge_id, inspection_year, created_at desc);
create index if not exists ix_inspection_year_deletion_audits_actor_time
  on inspection_year_deletion_audits (actor_user_id, created_at desc);

create table if not exists archived_file_deletion_queue (
  id uuid primary key default gen_random_uuid(),
  deletion_audit_id uuid not null references inspection_year_deletion_audits(id) on delete cascade,
  storage_relative_path text not null,
  status text not null default '待清理'
    check (status in ('待清理', '已完成', '失败待重试')),
  attempt_count integer not null default 0 check (attempt_count >= 0),
  last_error text,
  created_at timestamptz not null default now(),
  completed_at timestamptz,
  unique (deletion_audit_id, storage_relative_path),
  constraint archived_file_deletion_queue_relative_path_check check (
    storage_relative_path !~ '^[A-Za-z]:[\\/]'
    and storage_relative_path !~ '^[\\/]'
    and storage_relative_path not like '%..%'
  ),
  constraint archived_file_deletion_queue_completion_check check (
    (status = '已完成' and completed_at is not null)
    or (status <> '已完成' and completed_at is null)
  )
);

create index if not exists ix_archived_file_deletion_queue_pending
  on archived_file_deletion_queue (status, created_at)
  where status in ('待清理', '失败待重试');

