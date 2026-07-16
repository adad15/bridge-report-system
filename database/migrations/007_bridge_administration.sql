-- 007：管理员新增与批量永久删除桥梁档案。
-- 依赖 002、004、006；幂等可复跑。

create sequence if not exists bridge_deletion_audit_system_number_seq;

create table if not exists bridge_deletion_audits (
  id uuid primary key default gen_random_uuid(),
  system_number text not null unique default (
    'QLSC-' || lpad(nextval('bridge_deletion_audit_system_number_seq')::text, 6, '0')
  ),
  batch_id uuid not null,
  bridge_id uuid references bridges(id) on delete set null,
  bridge_system_number_snapshot text not null,
  bridge_name_snapshot text not null,
  route_number_snapshot text,
  route_name_snapshot text,
  station_mark_snapshot text,
  status_snapshot text not null,
  actor_user_id uuid references users(id) on delete set null,
  actor_username_snapshot text not null,
  actor_display_name_snapshot text not null,
  reason text not null check (length(btrim(reason)) > 0),
  impact_json jsonb not null default '{}'::jsonb check (jsonb_typeof(impact_json) = 'object'),
  deleted_counts_json jsonb not null default '{}'::jsonb check (jsonb_typeof(deleted_counts_json) = 'object'),
  file_cleanup_status text not null default '待清理'
    check (file_cleanup_status in ('待清理', '已完成', '部分失败')),
  created_at timestamptz not null default now(),
  file_cleanup_completed_at timestamptz,
  constraint bridge_deletion_audits_cleanup_completion_check check (
    (file_cleanup_status = '已完成' and file_cleanup_completed_at is not null)
    or (file_cleanup_status <> '已完成' and file_cleanup_completed_at is null)
  )
);

create index if not exists ix_bridge_deletion_audits_batch
  on bridge_deletion_audits (batch_id, created_at);
create index if not exists ix_bridge_deletion_audits_bridge_time
  on bridge_deletion_audits (bridge_id, created_at desc);
create index if not exists ix_bridge_deletion_audits_actor_time
  on bridge_deletion_audits (actor_user_id, created_at desc);

-- 将既有年度清理队列升级为可领取、可退避、可恢复的四态队列。
alter table archived_file_deletion_queue
  add column if not exists next_attempt_at timestamptz not null default now(),
  add column if not exists processing_started_at timestamptz;

alter table archived_file_deletion_queue
  drop constraint if exists archived_file_deletion_queue_status_check,
  drop constraint if exists archived_file_deletion_queue_completion_check;

alter table archived_file_deletion_queue
  add constraint archived_file_deletion_queue_status_check
    check (status in ('待清理', '清理中', '已完成', '失败待重试')),
  add constraint archived_file_deletion_queue_completion_check check (
    (status = '已完成' and completed_at is not null and processing_started_at is null)
    or (status = '清理中' and completed_at is null and processing_started_at is not null)
    or (status in ('待清理', '失败待重试') and completed_at is null and processing_started_at is null)
  );

drop index if exists ix_archived_file_deletion_queue_pending;
create index if not exists ix_archived_file_deletion_queue_claimable
  on archived_file_deletion_queue (next_attempt_at, created_at)
  where status in ('待清理', '失败待重试');
create index if not exists ix_archived_file_deletion_queue_stale_processing
  on archived_file_deletion_queue (processing_started_at)
  where status = '清理中';

create table if not exists bridge_archived_file_deletion_queue (
  id uuid primary key default gen_random_uuid(),
  bridge_deletion_audit_id uuid not null references bridge_deletion_audits(id) on delete cascade,
  storage_relative_path text not null,
  status text not null default '待清理'
    check (status in ('待清理', '清理中', '已完成', '失败待重试')),
  attempt_count integer not null default 0 check (attempt_count >= 0),
  last_error text,
  next_attempt_at timestamptz not null default now(),
  processing_started_at timestamptz,
  created_at timestamptz not null default now(),
  completed_at timestamptz,
  unique (bridge_deletion_audit_id, storage_relative_path),
  constraint bridge_archived_file_deletion_queue_relative_path_check check (
    storage_relative_path !~ '^[A-Za-z]:[\\/]'
    and storage_relative_path !~ '^[\\/]'
    and storage_relative_path not like '%..%'
  ),
  constraint bridge_archived_file_deletion_queue_completion_check check (
    (status = '已完成' and completed_at is not null and processing_started_at is null)
    or (status = '清理中' and completed_at is null and processing_started_at is not null)
    or (status in ('待清理', '失败待重试') and completed_at is null and processing_started_at is null)
  )
);

create index if not exists ix_bridge_file_deletion_queue_claimable
  on bridge_archived_file_deletion_queue (next_attempt_at, created_at)
  where status in ('待清理', '失败待重试');
create index if not exists ix_bridge_file_deletion_queue_stale_processing
  on bridge_archived_file_deletion_queue (processing_started_at)
  where status = '清理中';
