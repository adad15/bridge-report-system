-- 010：版本化规范包注册与年度项目规范组合。
-- 依赖 002、003、004；幂等可复跑，不回填或删除既有业务数据。

create table if not exists standard_packages (
  id uuid primary key default gen_random_uuid(),
  standard_family text not null
    check (standard_family in ('technical_condition', 'maintenance')),
  standard_id text not null check (length(btrim(standard_id)) > 0),
  standard_code text not null check (length(btrim(standard_code)) > 0),
  standard_name text not null check (length(btrim(standard_name)) > 0),
  official_edition text not null check (length(btrim(official_edition)) > 0),
  package_version text not null check (length(btrim(package_version)) > 0),
  contract_version integer not null check (contract_version > 0),
  algorithm_id text not null check (length(btrim(algorithm_id)) > 0),
  effective_date date not null,
  content_checksum text not null check (content_checksum ~ '^sha256:[0-9a-f]{64}$'),
  is_enabled boolean not null default true,
  sync_status text not null default '正常' check (sync_status in ('正常', '故障')),
  sync_error_code text,
  sync_error_message text,
  synchronized_at timestamptz not null default now(),
  created_at timestamptz not null default now(),
  updated_at timestamptz not null default now(),
  constraint standard_packages_identity_version_unique
    unique (standard_family, standard_id, package_version),
  constraint standard_packages_fault_detail_check check (
    (sync_status = '正常' and sync_error_code is null and sync_error_message is null)
    or (sync_status = '故障' and sync_error_code is not null and sync_error_message is not null)
  )
);

create index if not exists ix_standard_packages_family_enabled
  on standard_packages (standard_family, is_enabled, sync_status, effective_date desc);

create or replace function protect_standard_package_identity()
returns trigger
language plpgsql
as $$
begin
  if (new.standard_family, new.standard_id, new.standard_code, new.standard_name,
      new.official_edition, new.package_version, new.contract_version,
      new.algorithm_id, new.effective_date, new.content_checksum)
     is distinct from
     (old.standard_family, old.standard_id, old.standard_code, old.standard_name,
      old.official_edition, old.package_version, old.contract_version,
      old.algorithm_id, old.effective_date, old.content_checksum) then
    raise exception using
      errcode = '23514',
      constraint = 'standard_packages_identity_immutable',
      message = 'standard package identity and checksum are immutable';
  end if;
  return new;
end
$$;

drop trigger if exists trg_standard_packages_identity_immutable on standard_packages;
create trigger trg_standard_packages_identity_immutable
before update on standard_packages
for each row execute function protect_standard_package_identity();

create table if not exists project_standard_profiles (
  id uuid primary key default gen_random_uuid(),
  profile_series_id uuid not null default gen_random_uuid(),
  revision_number integer not null default 1 check (revision_number > 0),
  technical_condition_package_id uuid not null
    references standard_packages(id) on delete restrict,
  maintenance_package_id uuid not null
    references standard_packages(id) on delete restrict,
  supersedes_profile_id uuid references project_standard_profiles(id) on delete restrict,
  status text not null default '生效' check (status in ('生效', '已停用')),
  effective_at timestamptz not null default now(),
  created_by_user_id uuid not null references users(id) on delete restrict,
  change_reason text not null check (length(btrim(change_reason)) between 1 and 1000),
  created_at timestamptz not null default now(),
  updated_at timestamptz not null default now(),
  unique (profile_series_id, revision_number),
  constraint project_standard_profiles_distinct_packages_check
    check (technical_condition_package_id <> maintenance_package_id)
);

create index if not exists ix_project_standard_profiles_packages
  on project_standard_profiles (technical_condition_package_id, maintenance_package_id);
create index if not exists ix_project_standard_profiles_series_revision
  on project_standard_profiles (profile_series_id, revision_number desc);

alter table inspection_years
  add column if not exists standard_profile_id uuid;

do $$
begin
  if not exists (
    select 1 from pg_constraint
    where conname = 'inspection_years_standard_profile_id_fkey'
      and conrelid = 'inspection_years'::regclass
  ) then
    alter table inspection_years
      add constraint inspection_years_standard_profile_id_fkey
      foreign key (standard_profile_id)
      references project_standard_profiles(id)
      on delete restrict;
  end if;
end
$$;

create index if not exists ix_inspection_years_standard_profile
  on inspection_years (standard_profile_id)
  where standard_profile_id is not null;

create or replace function validate_project_standard_profile_families()
returns trigger
language plpgsql
as $$
declare
  v_technical_family text;
  v_maintenance_family text;
begin
  select standard_family into v_technical_family
  from standard_packages where id = new.technical_condition_package_id;
  select standard_family into v_maintenance_family
  from standard_packages where id = new.maintenance_package_id;

  if v_technical_family is distinct from 'technical_condition'
     or v_maintenance_family is distinct from 'maintenance' then
    raise exception using
      errcode = '23514',
      constraint = 'project_standard_profiles_family_check',
      message = 'project standard profile package family mismatch';
  end if;
  return new;
end
$$;

drop trigger if exists trg_project_standard_profiles_family on project_standard_profiles;
create trigger trg_project_standard_profiles_family
before insert or update of technical_condition_package_id, maintenance_package_id
on project_standard_profiles
for each row execute function validate_project_standard_profile_families();

create or replace function protect_formal_project_standard_profile()
returns trigger
language plpgsql
as $$
begin
  if exists (
    select 1
    from inspection_years iy
    where iy.standard_profile_id = old.id
      and (
        iy.status in ('已确认', '已被修订', '已归档')
        or exists (
          select 1 from condition_ratings cr
          where cr.inspection_year_id = iy.id and cr.review_status = '已确认'
        )
      )
  ) then
    raise exception using
      errcode = '23514',
      constraint = 'project_standard_profiles_formal_immutable',
      message = 'formal project standard profile is immutable';
  end if;
  return case when tg_op = 'DELETE' then old else new end;
end
$$;

drop trigger if exists trg_project_standard_profiles_formal_immutable
  on project_standard_profiles;
create trigger trg_project_standard_profiles_formal_immutable
before update or delete on project_standard_profiles
for each row execute function protect_formal_project_standard_profile();

comment on table standard_packages is
  '启动时从规则包目录同步的版本化规范身份；同一身份与包版本不得对应不同摘要。';
comment on table project_standard_profiles is
  '年度检测项目采用的技术评定标准与养护规范组合；正式结果形成后只能新增修订。';
comment on column inspection_years.standard_profile_id is
  '当前年度检测项目锁定的规范组合；历史空数据不由迁移自动回填。';
