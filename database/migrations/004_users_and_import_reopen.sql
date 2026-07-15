-- 004：轻量账号体系（users / user_sessions）+ 已确认导入记录重开校对审计列
--
-- 依赖 002_core_schema_and_archive.sql；幂等可复跑（psql -v ON_ERROR_STOP=1）。
--
-- 设计要点：
--   * 第一版不做注册/用户管理界面：默认账号由 C++ 后端启动时播种
--     （users 表为空才插入），密码哈希格式 pbkdf2_sha256$iter$salt_hex$hash_hex，
--     本文件不写死任何哈希值。
--   * 会话表只存 token 的 SHA-256 哈希，明文 token 只在登录响应里出现一次。
--   * import_records 的重开列描述"已确认记录被翻回待校对"的审计现场：
--     谁、何时、什么范围（warnings_only=仅警告病害可改 / full=全部可改）。
--     重新确认入库或放弃修改时三列一并清空。

create table if not exists users (
  id uuid primary key default gen_random_uuid(),
  username text not null unique,
  display_name text not null,
  password_hash text not null,
  role text not null check (role in ('admin', 'normal')),
  is_active boolean not null default true,
  created_at timestamptz not null default now(),
  updated_at timestamptz not null default now()
);

create table if not exists user_sessions (
  id uuid primary key default gen_random_uuid(),
  user_id uuid not null references users(id) on delete cascade,
  token_hash text not null unique,
  expires_at timestamptz not null,
  created_at timestamptz not null default now()
);

create index if not exists ix_user_sessions_user on user_sessions (user_id);
create index if not exists ix_user_sessions_expires on user_sessions (expires_at);

-- 重开校对审计列：reopened_at 非空即代表"当前处于重开态"。
-- reopen_backup_parsed_result_json 在重开那一刻快照 parsed_result_json，
-- 「放弃修改」时精确还原草稿（重新确认入库或放弃修改都会清空备份）。
alter table import_records
  add column if not exists reopened_at timestamptz,
  add column if not exists reopened_by_username text,
  add column if not exists reopen_scope text,
  add column if not exists reopen_backup_parsed_result_json jsonb;

do $$
begin
  if not exists (
    select 1 from pg_constraint
    where conname = 'import_records_reopen_scope_check'
      and conrelid = 'import_records'::regclass
  ) then
    alter table import_records
      add constraint import_records_reopen_scope_check
      check (reopen_scope is null or reopen_scope in ('warnings_only', 'full'));
  end if;
end
$$;

-- 三列要么全空（未重开），要么全非空（重开态/重开审计现场完整）。
do $$
begin
  if not exists (
    select 1 from pg_constraint
    where conname = 'import_records_reopen_fields_consistent_check'
      and conrelid = 'import_records'::regclass
  ) then
    alter table import_records
      add constraint import_records_reopen_fields_consistent_check
      check (
        (reopened_at is null and reopened_by_username is null and reopen_scope is null)
        or (reopened_at is not null and reopened_by_username is not null and reopen_scope is not null)
      );
  end if;
end
$$;
