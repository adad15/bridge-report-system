-- 018：版本化有效评定树、只读节点与年度病害引用。
-- 依赖 002、010、017；幂等可复跑，不猜测回填历史数据。

create table if not exists rating_tree_versions (
  id uuid primary key default gen_random_uuid(),
  tree_code text not null check (length(btrim(tree_code)) > 0),
  tree_name text not null check (length(btrim(tree_name)) > 0),
  package_version text not null check (length(btrim(package_version)) > 0),
  contract_version integer not null check (contract_version > 0),
  technical_condition_package_id uuid not null
    references standard_packages(id) on delete restrict,
  technical_condition_standard_id text not null
    check (length(btrim(technical_condition_standard_id)) > 0),
  technical_condition_package_version text not null
    check (length(btrim(technical_condition_package_version)) > 0),
  technical_condition_content_checksum text not null
    check (technical_condition_content_checksum ~ '^sha256:[0-9a-f]{64}$'),
  maintenance_package_id uuid not null
    references standard_packages(id) on delete restrict,
  maintenance_standard_id text not null
    check (length(btrim(maintenance_standard_id)) > 0),
  maintenance_package_version text not null
    check (length(btrim(maintenance_package_version)) > 0),
  maintenance_content_checksum text not null
    check (maintenance_content_checksum ~ '^sha256:[0-9a-f]{64}$'),
  organization_tree_code text not null
    check (length(btrim(organization_tree_code)) > 0),
  organization_package_version text not null
    check (length(btrim(organization_package_version)) > 0),
  organization_content_checksum text not null
    check (organization_content_checksum ~ '^sha256:[0-9a-f]{64}$'),
  tree_content_checksum text not null
    check (tree_content_checksum ~ '^sha256:[0-9a-f]{64}$'),
  status text not null default 'draft'
    check (status in ('draft', 'published', 'failed')),
  sync_error_code text,
  sync_error_message text,
  published_at timestamptz,
  created_at timestamptz not null default now(),
  updated_at timestamptz not null default now(),
  constraint rating_tree_versions_identity_unique
    unique (tree_code, package_version),
  constraint rating_tree_versions_checksum_unique
    unique (tree_content_checksum),
  constraint rating_tree_versions_publish_state_check check (
    (status = 'published' and published_at is not null
      and sync_error_code is null and sync_error_message is null)
    or (status = 'failed' and published_at is null
      and sync_error_code is not null and sync_error_message is not null)
    or (status = 'draft' and published_at is null)
  )
);

create index if not exists ix_rating_tree_versions_sources
  on rating_tree_versions (
    technical_condition_package_id, maintenance_package_id, status
  );

create table if not exists rating_tree_nodes (
  id uuid primary key default gen_random_uuid(),
  rating_tree_version_id uuid not null
    references rating_tree_versions(id) on delete cascade,
  node_key text not null check (length(btrim(node_key)) > 0),
  parent_node_id uuid references rating_tree_nodes(id) on delete restrict,
  display_name text not null check (length(btrim(display_name)) > 0),
  node_type text not null check (
    node_type in (
      'root', 'bridge_type_group', 'structure_group',
      'component_group', 'defect', 'placeholder'
    )
  ),
  sort_order integer not null default 0,
  bridge_type_ids text[] not null default '{}'::text[],
  component_category_ids text[] not null default '{}'::text[],
  scoring_mode text not null check (
    scoring_mode in ('inherit_h21', 'reference_h21', 'non_scoring')
  ),
  h21_indicator_id text,
  is_selectable boolean not null default false,
  is_scoring boolean not null default false,
  organization_note text not null default '',
  allowed_scales integer[] not null default '{}'::integer[],
  detail_json jsonb not null default '{}'::jsonb,
  created_at timestamptz not null default now(),
  constraint rating_tree_nodes_key_unique
    unique (rating_tree_version_id, node_key),
  constraint rating_tree_nodes_detail_json_check
    check (jsonb_typeof(detail_json) = 'object'),
  constraint rating_tree_nodes_scoring_target_check check (
    (
      scoring_mode in ('inherit_h21', 'reference_h21')
      and node_type = 'defect'
      and h21_indicator_id is not null
      and is_selectable
      and is_scoring
      and cardinality(allowed_scales) > 0
    )
    or (
      scoring_mode = 'non_scoring'
      and h21_indicator_id is null
      and not is_scoring
    )
  )
);

create index if not exists ix_rating_tree_nodes_parent_order
  on rating_tree_nodes (rating_tree_version_id, parent_node_id, sort_order, node_key);
create index if not exists ix_rating_tree_nodes_scoring_lookup
  on rating_tree_nodes (
    rating_tree_version_id, h21_indicator_id, is_selectable
  )
  where is_scoring;
create index if not exists ix_rating_tree_nodes_bridge_scope
  on rating_tree_nodes using gin (bridge_type_ids);
create index if not exists ix_rating_tree_nodes_component_scope
  on rating_tree_nodes using gin (component_category_ids);

create table if not exists rating_tree_node_sources (
  id uuid primary key default gen_random_uuid(),
  rating_tree_node_id uuid not null
    references rating_tree_nodes(id) on delete cascade,
  source_key text not null check (length(btrim(source_key)) > 0),
  source_type text not null check (
    source_type in ('h21', 'jtg5120', 'organization')
  ),
  title text not null check (length(btrim(title)) > 0),
  source_reference text not null default '',
  source_rule_id text,
  created_at timestamptz not null default now(),
  unique (rating_tree_node_id, source_key)
);

create index if not exists ix_rating_tree_node_sources_type
  on rating_tree_node_sources (source_type, source_key);

create table if not exists rating_tree_aliases (
  id uuid primary key default gen_random_uuid(),
  rating_tree_version_id uuid not null
    references rating_tree_versions(id) on delete cascade,
  target_node_id uuid not null
    references rating_tree_nodes(id) on delete cascade,
  bridge_type_id text not null check (length(btrim(bridge_type_id)) > 0),
  component_category_id text not null
    check (length(btrim(component_category_id)) > 0),
  alias_text text not null check (length(btrim(alias_text)) > 0),
  normalized_alias text not null check (length(btrim(normalized_alias)) > 0),
  created_at timestamptz not null default now(),
  constraint rating_tree_aliases_scope_unique unique (
    rating_tree_version_id, bridge_type_id,
    component_category_id, normalized_alias
  )
);

create index if not exists ix_rating_tree_aliases_target
  on rating_tree_aliases (target_node_id);

alter table project_standard_profiles
  add column if not exists rating_tree_version_id uuid;

do $$
begin
  if not exists (
    select 1 from pg_constraint
    where conname = 'project_standard_profiles_rating_tree_version_id_fkey'
      and conrelid = 'project_standard_profiles'::regclass
  ) then
    alter table project_standard_profiles
      add constraint project_standard_profiles_rating_tree_version_id_fkey
      foreign key (rating_tree_version_id)
      references rating_tree_versions(id)
      on delete restrict;
  end if;
end
$$;

create index if not exists ix_project_standard_profiles_rating_tree
  on project_standard_profiles (rating_tree_version_id)
  where rating_tree_version_id is not null;

alter table defect_observations
  add column if not exists rating_tree_node_id uuid;

do $$
begin
  if not exists (
    select 1 from pg_constraint
    where conname = 'defect_observations_rating_tree_node_id_fkey'
      and conrelid = 'defect_observations'::regclass
  ) then
    alter table defect_observations
      add constraint defect_observations_rating_tree_node_id_fkey
      foreign key (rating_tree_node_id)
      references rating_tree_nodes(id)
      on delete restrict;
  end if;
end
$$;

create index if not exists ix_defect_observations_rating_tree_node
  on defect_observations (
    inspection_year_id, bridge_component_id, rating_tree_node_id
  )
  where rating_tree_node_id is not null;

create or replace function validate_rating_tree_version_sources()
returns trigger
language plpgsql
as $$
declare
  v_technical standard_packages%rowtype;
  v_maintenance standard_packages%rowtype;
begin
  select * into v_technical
  from standard_packages where id = new.technical_condition_package_id;
  select * into v_maintenance
  from standard_packages where id = new.maintenance_package_id;

  if v_technical.standard_family is distinct from 'technical_condition'
     or v_technical.standard_id is distinct from new.technical_condition_standard_id
     or v_technical.package_version is distinct from new.technical_condition_package_version
     or v_technical.content_checksum is distinct from new.technical_condition_content_checksum then
    raise exception using
      errcode = '23514',
      constraint = 'rating_tree_versions_technical_source_check',
      message = 'rating tree technical-condition source does not match its package';
  end if;

  if v_maintenance.standard_family is distinct from 'maintenance'
     or v_maintenance.standard_id is distinct from new.maintenance_standard_id
     or v_maintenance.package_version is distinct from new.maintenance_package_version
     or v_maintenance.content_checksum is distinct from new.maintenance_content_checksum then
    raise exception using
      errcode = '23514',
      constraint = 'rating_tree_versions_maintenance_source_check',
      message = 'rating tree maintenance source does not match its package';
  end if;
  return new;
end
$$;

create or replace function validate_rating_tree_node_parent()
returns trigger
language plpgsql
as $$
declare
  v_parent_version_id uuid;
begin
  if new.parent_node_id is null then
    return new;
  end if;
  select rating_tree_version_id into v_parent_version_id
  from rating_tree_nodes where id = new.parent_node_id;
  if v_parent_version_id is distinct from new.rating_tree_version_id then
    raise exception using
      errcode = '23514',
      constraint = 'rating_tree_nodes_parent_version_check',
      message = 'rating tree node and parent must belong to the same version';
  end if;
  return new;
end
$$;

create or replace function validate_rating_tree_alias_target()
returns trigger
language plpgsql
as $$
declare
  v_target_version_id uuid;
  v_target_selectable boolean;
begin
  select rating_tree_version_id, is_selectable
    into v_target_version_id, v_target_selectable
  from rating_tree_nodes where id = new.target_node_id;
  if v_target_version_id is distinct from new.rating_tree_version_id
     or not coalesce(v_target_selectable, false) then
    raise exception using
      errcode = '23514',
      constraint = 'rating_tree_aliases_target_check',
      message = 'rating tree alias must target a selectable node in the same version';
  end if;
  return new;
end
$$;

create or replace function validate_project_standard_profile_rating_tree()
returns trigger
language plpgsql
as $$
declare
  v_technical_package_id uuid;
  v_maintenance_package_id uuid;
  v_status text;
begin
  if new.rating_tree_version_id is null then
    return new;
  end if;
  select technical_condition_package_id, maintenance_package_id, status
    into v_technical_package_id, v_maintenance_package_id, v_status
  from rating_tree_versions where id = new.rating_tree_version_id;
  if v_technical_package_id is distinct from new.technical_condition_package_id
     or v_maintenance_package_id is distinct from new.maintenance_package_id then
    raise exception using
      errcode = '23514',
      constraint = 'project_standard_profiles_rating_tree_packages_check',
      message = 'project standard profile packages do not match its rating tree';
  end if;
  if v_status is distinct from 'published' then
    raise exception using
      errcode = '23514',
      constraint = 'project_standard_profiles_rating_tree_published_check',
      message = 'project standard profile requires a published rating tree';
  end if;
  return new;
end
$$;

create or replace function validate_defect_rating_tree_node()
returns trigger
language plpgsql
as $$
declare
  v_profile_tree_id uuid;
  v_node_tree_id uuid;
  v_h21_indicator_id text;
  v_selectable boolean;
begin
  if new.rating_tree_node_id is null then
    return new;
  end if;
  select p.rating_tree_version_id into v_profile_tree_id
  from inspection_years iy
  join project_standard_profiles p on p.id = iy.standard_profile_id
  where iy.id = new.inspection_year_id;
  select rating_tree_version_id, h21_indicator_id, is_selectable
    into v_node_tree_id, v_h21_indicator_id, v_selectable
  from rating_tree_nodes where id = new.rating_tree_node_id;

  if v_profile_tree_id is null
     or v_node_tree_id is distinct from v_profile_tree_id
     or not coalesce(v_selectable, false) then
    raise exception using
      errcode = '23514',
      constraint = 'defect_observations_rating_tree_context_check',
      message = 'defect rating tree node does not belong to the inspection year profile';
  end if;
  if new.standard_defect_indicator_id is distinct from v_h21_indicator_id then
    raise exception using
      errcode = '23514',
      constraint = 'defect_observations_rating_tree_indicator_check',
      message = 'defect H21 indicator does not match its rating tree node';
  end if;
  return new;
end
$$;

create or replace function protect_published_rating_tree_version()
returns trigger
language plpgsql
as $$
begin
  if old.status = 'published' then
    raise exception using
      errcode = '23514',
      constraint = 'rating_tree_versions_published_immutable',
      message = 'published rating tree version is immutable';
  end if;
  if tg_op = 'UPDATE'
     and new.status = 'published'
     and not exists (
       select 1 from rating_tree_nodes
       where rating_tree_version_id = old.id
     ) then
    raise exception using
      errcode = '23514',
      constraint = 'rating_tree_versions_publish_requires_nodes',
      message = 'rating tree cannot be published without nodes';
  end if;
  return case when tg_op = 'DELETE' then old else new end;
end
$$;

create or replace function protect_published_rating_tree_child()
returns trigger
language plpgsql
as $$
declare
  v_version_id uuid;
  v_status text;
begin
  if tg_table_name = 'rating_tree_nodes' then
    v_version_id := case when tg_op = 'DELETE'
      then old.rating_tree_version_id else new.rating_tree_version_id end;
  elsif tg_table_name = 'rating_tree_aliases' then
    v_version_id := case when tg_op = 'DELETE'
      then old.rating_tree_version_id else new.rating_tree_version_id end;
  else
    select rating_tree_version_id into v_version_id
    from rating_tree_nodes
    where id = case when tg_op = 'DELETE'
      then old.rating_tree_node_id else new.rating_tree_node_id end;
  end if;

  select status into v_status from rating_tree_versions where id = v_version_id;
  if v_status = 'published' then
    raise exception using
      errcode = '23514',
      constraint = 'rating_tree_published_children_immutable',
      message = 'published rating tree contents are immutable';
  end if;
  return case when tg_op = 'DELETE' then old else new end;
end
$$;

drop trigger if exists trg_rating_tree_versions_sources on rating_tree_versions;
create trigger trg_rating_tree_versions_sources
before insert or update of
  technical_condition_package_id, technical_condition_standard_id,
  technical_condition_package_version, technical_condition_content_checksum,
  maintenance_package_id, maintenance_standard_id,
  maintenance_package_version, maintenance_content_checksum
on rating_tree_versions
for each row execute function validate_rating_tree_version_sources();

drop trigger if exists trg_rating_tree_versions_published_immutable
  on rating_tree_versions;
create trigger trg_rating_tree_versions_published_immutable
before update or delete on rating_tree_versions
for each row execute function protect_published_rating_tree_version();

drop trigger if exists trg_rating_tree_nodes_parent on rating_tree_nodes;
create trigger trg_rating_tree_nodes_parent
before insert or update of rating_tree_version_id, parent_node_id
on rating_tree_nodes
for each row execute function validate_rating_tree_node_parent();

drop trigger if exists trg_rating_tree_aliases_target on rating_tree_aliases;
create trigger trg_rating_tree_aliases_target
before insert or update of rating_tree_version_id, target_node_id
on rating_tree_aliases
for each row execute function validate_rating_tree_alias_target();

drop trigger if exists trg_project_standard_profiles_rating_tree
  on project_standard_profiles;
create trigger trg_project_standard_profiles_rating_tree
before insert or update of
  technical_condition_package_id, maintenance_package_id, rating_tree_version_id
on project_standard_profiles
for each row execute function validate_project_standard_profile_rating_tree();

drop trigger if exists trg_defect_observations_rating_tree_node
  on defect_observations;
create trigger trg_defect_observations_rating_tree_node
before insert or update of
  inspection_year_id, standard_defect_indicator_id, rating_tree_node_id
on defect_observations
for each row execute function validate_defect_rating_tree_node();

drop trigger if exists trg_rating_tree_nodes_published_immutable
  on rating_tree_nodes;
create trigger trg_rating_tree_nodes_published_immutable
before insert or update or delete on rating_tree_nodes
for each row execute function protect_published_rating_tree_child();

drop trigger if exists trg_rating_tree_node_sources_published_immutable
  on rating_tree_node_sources;
create trigger trg_rating_tree_node_sources_published_immutable
before insert or update or delete on rating_tree_node_sources
for each row execute function protect_published_rating_tree_child();

drop trigger if exists trg_rating_tree_aliases_published_immutable
  on rating_tree_aliases;
create trigger trg_rating_tree_aliases_published_immutable
before insert or update or delete on rating_tree_aliases
for each row execute function protect_published_rating_tree_child();

comment on table rating_tree_versions is
  '由 H21、JTG 5120 和单位规则编译出的不可变有效桥梁评定树版本。';
comment on table rating_tree_nodes is
  '有效评定树的只读节点；评分叶节点显式解析到 H21 病害指标。';
comment on table rating_tree_node_sources is
  '评定树节点的 H21、JTG 5120 和单位规则多来源证据。';
comment on table rating_tree_aliases is
  '仅在桥型和构件类别范围内生效的唯一受控病害别名。';
comment on column project_standard_profiles.rating_tree_version_id is
  '年度规范组合锁定的已发布有效评定树；历史空数据不由迁移猜测回填。';
comment on column defect_observations.rating_tree_node_id is
  '病害在该年度有效评定树中的只读节点引用。';
