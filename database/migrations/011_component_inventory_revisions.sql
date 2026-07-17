-- 011：版本化桥梁实际构件台账、规范类别映射和年度台账锁定。
-- 依赖 002、004、010；幂等可复跑，不回填既有桥梁或年度数据。

create table if not exists bridge_component_generation_batches (
  id uuid primary key default gen_random_uuid(),
  bridge_id uuid not null references bridges(id) on delete cascade,
  template_standard_package_id uuid not null
    references standard_packages(id) on delete restrict,
  template_id text not null check (length(btrim(template_id)) > 0),
  bridge_type_code text not null check (length(btrim(bridge_type_code)) > 0),
  input_quantities jsonb not null default '{}'::jsonb
    check (jsonb_typeof(input_quantities) = 'object'),
  generated_by_user_id uuid not null references users(id) on delete restrict,
  generated_at timestamptz not null default now()
);

create index if not exists ix_component_generation_batches_bridge_time
  on bridge_component_generation_batches (bridge_id, generated_at desc);

create or replace function validate_component_generation_batch_standard()
returns trigger
language plpgsql
as $$
declare
  v_standard_family text;
begin
  select standard_family into v_standard_family
  from standard_packages where id = new.template_standard_package_id;

  if v_standard_family is distinct from 'technical_condition' then
    raise exception using
      errcode = '23514',
      constraint = 'component_generation_batch_package_family',
      message = 'component inventory generation requires a technical-condition package';
  end if;

  return new;
end
$$;

drop trigger if exists trg_component_generation_batch_standard
  on bridge_component_generation_batches;
create trigger trg_component_generation_batch_standard
before insert or update of template_standard_package_id
on bridge_component_generation_batches
for each row execute function validate_component_generation_batch_standard();

create table if not exists bridge_component_inventory_revisions (
  id uuid primary key default gen_random_uuid(),
  bridge_id uuid not null references bridges(id) on delete cascade,
  revision_number integer not null check (revision_number > 0),
  status text not null default '草稿' check (status in ('草稿', '已确认')),
  baseline_revision_id uuid
    references bridge_component_inventory_revisions(id) on delete restrict,
  created_by_user_id uuid not null references users(id) on delete restrict,
  confirmed_by_user_id uuid references users(id) on delete restrict,
  confirmed_at timestamptz,
  confirmation_note text,
  created_at timestamptz not null default now(),
  updated_at timestamptz not null default now(),
  unique (bridge_id, revision_number),
  constraint component_inventory_revision_confirmation_check check (
    (status = '草稿' and confirmed_by_user_id is null and confirmed_at is null)
    or (status = '已确认' and confirmed_by_user_id is not null and confirmed_at is not null)
  ),
  constraint component_inventory_revision_baseline_self_check
    check (baseline_revision_id is null or baseline_revision_id <> id)
);

create index if not exists ix_component_inventory_revisions_bridge_status
  on bridge_component_inventory_revisions (bridge_id, status, revision_number desc);

create table if not exists bridge_component_inventory_entries (
  id uuid primary key default gen_random_uuid(),
  inventory_revision_id uuid not null
    references bridge_component_inventory_revisions(id) on delete cascade,
  bridge_component_id uuid not null
    references bridge_components(id) on delete cascade,
  generation_batch_id uuid
    references bridge_component_generation_batches(id) on delete restrict,
  component_number text not null check (length(btrim(component_number)) > 0),
  site_name text not null check (length(btrim(site_name)) > 0),
  site_component_type text not null check (length(btrim(site_component_type)) > 0),
  span_or_location text,
  is_active boolean not null default true,
  deactivated_at timestamptz,
  deactivation_reason text,
  sort_order integer not null default 0 check (sort_order >= 0),
  remarks text,
  created_at timestamptz not null default now(),
  updated_at timestamptz not null default now(),
  unique (inventory_revision_id, bridge_component_id),
  unique (inventory_revision_id, site_component_type, component_number),
  constraint component_inventory_entry_deactivation_check check (
    (is_active and deactivated_at is null and deactivation_reason is null)
    or (
      not is_active
      and deactivated_at is not null
      and deactivation_reason is not null
      and length(btrim(deactivation_reason)) > 0
    )
  )
);

create index if not exists ix_component_inventory_entries_component
  on bridge_component_inventory_entries (bridge_component_id, inventory_revision_id);
create index if not exists ix_component_inventory_entries_revision_order
  on bridge_component_inventory_entries (inventory_revision_id, is_active, sort_order, id);

create table if not exists bridge_component_standard_mappings (
  id uuid primary key default gen_random_uuid(),
  inventory_entry_id uuid not null
    references bridge_component_inventory_entries(id) on delete cascade,
  standard_package_id uuid not null
    references standard_packages(id) on delete restrict,
  standard_bridge_type_id text not null
    check (length(btrim(standard_bridge_type_id)) > 0),
  standard_component_category_id text not null
    check (length(btrim(standard_component_category_id)) > 0),
  structure_part text not null
    check (structure_part in ('superstructure', 'substructure', 'deck_system', 'overall', 'other')),
  mapping_source text not null check (length(btrim(mapping_source)) > 0),
  confirmation_status text not null default '待确认'
    check (confirmation_status in ('待确认', '已确认')),
  confirmed_by_user_id uuid references users(id) on delete restrict,
  confirmed_at timestamptz,
  is_active boolean not null default true,
  created_at timestamptz not null default now(),
  updated_at timestamptz not null default now(),
  constraint component_standard_mapping_confirmation_check check (
    (confirmation_status = '待确认' and confirmed_by_user_id is null and confirmed_at is null)
    or (confirmation_status = '已确认' and confirmed_by_user_id is not null and confirmed_at is not null)
  )
);

create unique index if not exists ux_component_standard_mappings_active
  on bridge_component_standard_mappings (inventory_entry_id, standard_package_id)
  where is_active;
create index if not exists ix_component_standard_mappings_package_category
  on bridge_component_standard_mappings (
    standard_package_id, standard_bridge_type_id, standard_component_category_id
  );

alter table inspection_years
  add column if not exists component_inventory_revision_id uuid;

do $$
begin
  if not exists (
    select 1 from pg_constraint
    where conname = 'inspection_years_component_inventory_revision_id_fkey'
      and conrelid = 'inspection_years'::regclass
  ) then
    alter table inspection_years
      add constraint inspection_years_component_inventory_revision_id_fkey
      foreign key (component_inventory_revision_id)
      references bridge_component_inventory_revisions(id)
      on delete restrict;
  end if;
end
$$;

create index if not exists ix_inspection_years_component_inventory_revision
  on inspection_years (component_inventory_revision_id)
  where component_inventory_revision_id is not null;

create or replace function validate_component_inventory_revision_context()
returns trigger
language plpgsql
as $$
declare
  v_baseline_bridge_id uuid;
  v_baseline_revision_number integer;
begin
  if tg_op = 'UPDATE' and old.status = '已确认' and new is distinct from old then
    raise exception using
      errcode = '23514',
      constraint = 'component_inventory_revision_confirmed_immutable',
      message = 'confirmed component inventory revision is immutable';
  end if;

  if new.baseline_revision_id is not null then
    select bridge_id, revision_number
      into v_baseline_bridge_id, v_baseline_revision_number
    from bridge_component_inventory_revisions
    where id = new.baseline_revision_id;

    if v_baseline_bridge_id is distinct from new.bridge_id
       or v_baseline_revision_number >= new.revision_number then
      raise exception using
        errcode = '23514',
        constraint = 'component_inventory_revision_baseline_context',
        message = 'inventory baseline must be an earlier revision of the same bridge';
    end if;
  end if;

  return new;
end
$$;

drop trigger if exists trg_component_inventory_revision_context
  on bridge_component_inventory_revisions;
create trigger trg_component_inventory_revision_context
before insert or update on bridge_component_inventory_revisions
for each row execute function validate_component_inventory_revision_context();

create or replace function validate_component_inventory_entry_context()
returns trigger
language plpgsql
as $$
declare
  v_revision_bridge_id uuid;
  v_component_bridge_id uuid;
  v_batch_bridge_id uuid;
begin
  select bridge_id into v_revision_bridge_id
  from bridge_component_inventory_revisions where id = new.inventory_revision_id;
  select bridge_id into v_component_bridge_id
  from bridge_components where id = new.bridge_component_id;

  if v_revision_bridge_id is distinct from v_component_bridge_id then
    raise exception using
      errcode = '23514',
      constraint = 'component_inventory_entry_bridge_context',
      message = 'inventory entry revision and physical component must belong to the same bridge';
  end if;

  if new.generation_batch_id is not null then
    select bridge_id into v_batch_bridge_id
    from bridge_component_generation_batches where id = new.generation_batch_id;
    if v_batch_bridge_id is distinct from v_revision_bridge_id then
      raise exception using
        errcode = '23514',
        constraint = 'component_inventory_entry_generation_batch_context',
        message = 'inventory entry generation batch must belong to the same bridge';
    end if;
  end if;

  return new;
end
$$;

create or replace function protect_confirmed_component_inventory_entry()
returns trigger
language plpgsql
as $$
declare
  v_revision_id uuid;
  v_revision_status text;
  v_bridge_id uuid;
begin
  v_revision_id := case when tg_op = 'INSERT' then new.inventory_revision_id else old.inventory_revision_id end;
  select status, bridge_id into v_revision_status, v_bridge_id
  from bridge_component_inventory_revisions where id = v_revision_id;

  -- 整桥删除时父桥梁已不可见，允许外键级联清理已确认快照。
  if v_revision_status = '已确认'
     and exists (select 1 from bridges where id = v_bridge_id) then
    raise exception using
      errcode = '23514',
      constraint = 'component_inventory_entries_confirmed_immutable',
      message = 'entries of a confirmed component inventory revision are immutable';
  end if;

  return case when tg_op = 'DELETE' then old else new end;
end
$$;

drop trigger if exists trg_component_inventory_entries_confirmed_immutable
  on bridge_component_inventory_entries;
create trigger trg_component_inventory_entries_confirmed_immutable
before insert or update or delete on bridge_component_inventory_entries
for each row execute function protect_confirmed_component_inventory_entry();

drop trigger if exists trg_component_inventory_entry_context
  on bridge_component_inventory_entries;
create trigger trg_component_inventory_entry_context
before insert or update of inventory_revision_id, bridge_component_id, generation_batch_id
on bridge_component_inventory_entries
for each row execute function validate_component_inventory_entry_context();

create or replace function validate_component_standard_mapping_context()
returns trigger
language plpgsql
as $$
declare
  v_standard_family text;
begin
  select standard_family into v_standard_family
  from standard_packages where id = new.standard_package_id;

  if v_standard_family is distinct from 'technical_condition' then
    raise exception using
      errcode = '23514',
      constraint = 'component_standard_mapping_package_family',
      message = 'component standard mapping requires a technical-condition package';
  end if;

  return new;
end
$$;

create or replace function protect_confirmed_component_standard_mapping()
returns trigger
language plpgsql
as $$
declare
  v_entry_id uuid;
  v_revision_status text;
  v_bridge_id uuid;
begin
  v_entry_id := case when tg_op = 'INSERT' then new.inventory_entry_id else old.inventory_entry_id end;
  select r.status, r.bridge_id into v_revision_status, v_bridge_id
  from bridge_component_inventory_entries e
  join bridge_component_inventory_revisions r on r.id = e.inventory_revision_id
  where e.id = v_entry_id;

  if v_revision_status = '已确认'
     and exists (select 1 from bridges where id = v_bridge_id) then
    raise exception using
      errcode = '23514',
      constraint = 'component_standard_mappings_confirmed_immutable',
      message = 'mappings of a confirmed component inventory revision are immutable';
  end if;

  return case when tg_op = 'DELETE' then old else new end;
end
$$;

drop trigger if exists trg_component_standard_mappings_confirmed_immutable
  on bridge_component_standard_mappings;
create trigger trg_component_standard_mappings_confirmed_immutable
before insert or update or delete on bridge_component_standard_mappings
for each row execute function protect_confirmed_component_standard_mapping();

drop trigger if exists trg_component_standard_mapping_context
  on bridge_component_standard_mappings;
create trigger trg_component_standard_mapping_context
before insert or update of standard_package_id
on bridge_component_standard_mappings
for each row execute function validate_component_standard_mapping_context();

create or replace function validate_inspection_year_inventory_revision()
returns trigger
language plpgsql
as $$
declare
  v_revision_bridge_id uuid;
  v_revision_status text;
begin
  if new.component_inventory_revision_id is null then
    return new;
  end if;

  select bridge_id, status into v_revision_bridge_id, v_revision_status
  from bridge_component_inventory_revisions
  where id = new.component_inventory_revision_id;

  if v_revision_bridge_id is distinct from new.bridge_id then
    raise exception using
      errcode = '23514',
      constraint = 'inspection_year_inventory_revision_bridge_context',
      message = 'inspection year and component inventory revision must belong to the same bridge';
  end if;

  if new.status in ('已确认', '已被修订', '已归档')
     and v_revision_status is distinct from '已确认' then
    raise exception using
      errcode = '23514',
      constraint = 'inspection_year_inventory_revision_confirmed',
      message = 'formal inspection year requires a confirmed component inventory revision';
  end if;

  return new;
end
$$;

drop trigger if exists trg_inspection_year_inventory_revision
  on inspection_years;
create trigger trg_inspection_year_inventory_revision
before insert or update of bridge_id, status, component_inventory_revision_id
on inspection_years
for each row execute function validate_inspection_year_inventory_revision();

comment on table bridge_component_generation_batches is
  '按版本化规范模板生成实际物理构件建议时保存的来源、桥型和数量输入。';
comment on table bridge_component_inventory_revisions is
  '桥梁实际物理构件台账的草稿或已确认版本；历史年度和正式评定锁定具体版本。';
comment on table bridge_component_inventory_entries is
  '某一台账版本中的可编辑编号、现场名称、现场类型、跨位和启停快照。';
comment on table bridge_component_standard_mappings is
  '台账条目到版本化技术评定规范类别及内部结构部位的映射。';
comment on column inspection_years.component_inventory_revision_id is
  '年度检测项目锁定的构件台账版本；既有年度不由迁移自动回填。';
comment on column bridge_components.structure_part is
  '旧数据投影；新代码使用台账条目的版本化规范映射确定内部结构部位。';
comment on column bridge_components.component_type is
  '旧数据投影；新代码使用 bridge_component_inventory_entries.site_component_type。';
comment on column bridge_components.business_component_code is
  '旧数据投影；新代码使用 bridge_component_inventory_entries.component_number。';
