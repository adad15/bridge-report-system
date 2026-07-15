-- 005：导入记录独占编辑租约锁与管理员强制释放审计
-- 依赖 002、004；幂等可复跑。

create table if not exists import_record_edit_locks (
  import_record_id uuid primary key references import_records(id) on delete cascade,
  user_id uuid not null references users(id) on delete cascade,
  user_session_id uuid not null references user_sessions(id) on delete cascade,
  lock_token_hash text not null unique,
  acquired_at timestamptz not null default now(),
  last_heartbeat_at timestamptz not null default now(),
  expires_at timestamptz not null,
  check (expires_at > acquired_at)
);

create index if not exists ix_import_record_edit_locks_expires
  on import_record_edit_locks (expires_at);
create index if not exists ix_import_record_edit_locks_user_session
  on import_record_edit_locks (user_session_id);

create table if not exists import_record_edit_lock_events (
  id uuid primary key default gen_random_uuid(),
  import_record_id uuid not null references import_records(id) on delete cascade,
  action text not null check (action in ('force_released')),
  previous_owner_user_id uuid references users(id) on delete set null,
  previous_lock_token_hash text,
  actor_user_id uuid not null references users(id) on delete restrict,
  reason text not null check (length(btrim(reason)) > 0),
  occurred_at timestamptz not null default now()
);

create index if not exists ix_import_record_edit_lock_events_record_time
  on import_record_edit_lock_events (import_record_id, occurred_at desc);
create index if not exists ix_import_record_edit_lock_events_previous_token
  on import_record_edit_lock_events (previous_lock_token_hash)
  where previous_lock_token_hash is not null;
