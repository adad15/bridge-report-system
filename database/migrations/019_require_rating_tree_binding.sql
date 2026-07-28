-- 019：强制新业务数据使用已发布评定树，并为历史评定补充不可变树身份。
-- 依赖 010、011、013、017、018；幂等可复跑，不重算或覆盖任何历史评分结果。

-- 兼容 018 早期开发库使用的 h21/jtg5120 枚举名。当前契约统一按来源职责命名，
-- 这里只归一来源标签，不改节点、规则内容或校验和。
alter table rating_tree_node_sources
  disable trigger trg_rating_tree_node_sources_published_immutable;
alter table rating_tree_node_sources
  drop constraint if exists rating_tree_node_sources_source_type_check;
update rating_tree_node_sources
set source_type = case source_type
  when 'h21' then 'technical_condition'
  when 'jtg5120' then 'maintenance'
  else source_type
end
where source_type in ('h21', 'jtg5120');
alter table rating_tree_node_sources
  add constraint rating_tree_node_sources_source_type_check check (
    source_type in ('technical_condition', 'maintenance', 'organization')
  );
alter table rating_tree_node_sources
  enable trigger trg_rating_tree_node_sources_published_immutable;

alter table assessment_runs
  add column if not exists rating_tree_version_id uuid,
  add column if not exists rating_tree_content_checksum text;

do $$
begin
  if not exists (
    select 1 from pg_constraint
    where conname = 'assessment_runs_rating_tree_version_id_fkey'
      and conrelid = 'assessment_runs'::regclass
  ) then
    alter table assessment_runs
      add constraint assessment_runs_rating_tree_version_id_fkey
      foreign key (rating_tree_version_id)
      references rating_tree_versions(id)
      on delete restrict;
  end if;
  if not exists (
    select 1 from pg_constraint
    where conname = 'assessment_runs_rating_tree_identity_check'
      and conrelid = 'assessment_runs'::regclass
  ) then
    alter table assessment_runs
      add constraint assessment_runs_rating_tree_identity_check check (
        (rating_tree_version_id is null and rating_tree_content_checksum is null)
        or (
          rating_tree_version_id is not null
          and rating_tree_content_checksum ~ '^sha256:[0-9a-f]{64}$'
        )
      );
  end if;
end
$$;

create index if not exists ix_assessment_runs_rating_tree
  on assessment_runs (rating_tree_version_id, created_at desc)
  where rating_tree_version_id is not null;

-- 只在同一 H21/JTG 5120 组合恰好对应一个已发布树版本时自动回填。
-- 正式 profile 的不可变触发器在本段受控关闭；这里只补树身份，不改规范包或评分。
alter table project_standard_profiles
  disable trigger trg_project_standard_profiles_formal_immutable;

with profile_candidates as (
  select
    p.id as profile_id,
    (array_agg(rtv.id order by rtv.published_at desc, rtv.id))[1] as tree_id,
    count(*) as candidate_count
  from project_standard_profiles p
  join rating_tree_versions rtv
    on rtv.technical_condition_package_id = p.technical_condition_package_id
   and rtv.maintenance_package_id = p.maintenance_package_id
   and rtv.status = 'published'
  where p.rating_tree_version_id is null
  group by p.id
)
update project_standard_profiles p
set rating_tree_version_id = c.tree_id
from profile_candidates c
where p.id = c.profile_id
  and c.candidate_count = 1
  and p.rating_tree_version_id is null;

alter table project_standard_profiles
  enable trigger trg_project_standard_profiles_formal_immutable;

-- 旧病害必须同时满足：年度已锁树、H21 指标一致、当前台账桥型/构件类别适用，
-- 且最终只有一个候选节点。多个单位节点复用同一 H21 指标时绝不猜测。
with defect_candidates as (
  select
    observation.id as observation_id,
    (array_agg(node.id order by node.sort_order, node.node_key))[1] as node_id,
    count(*) as candidate_count
  from defect_observations observation
  join inspection_years iy on iy.id = observation.inspection_year_id
  join project_standard_profiles profile on profile.id = iy.standard_profile_id
  join bridge_component_inventory_entries entry
    on entry.inventory_revision_id = iy.component_inventory_revision_id
   and entry.bridge_component_id = observation.bridge_component_id
   and entry.is_active
  join bridge_component_standard_mappings mapping
    on mapping.inventory_entry_id = entry.id
   and mapping.standard_package_id = profile.technical_condition_package_id
   and mapping.is_active
   and mapping.confirmation_status = '已确认'
  join rating_tree_nodes node
    on node.rating_tree_version_id = profile.rating_tree_version_id
   and node.h21_indicator_id = observation.standard_defect_indicator_id
   and node.is_selectable
   and mapping.standard_bridge_type_id = any(node.bridge_type_ids)
   and mapping.standard_component_category_id = any(node.component_category_ids)
  where observation.rating_tree_node_id is null
    and profile.rating_tree_version_id is not null
    and observation.standard_defect_indicator_id is not null
  group by observation.id
)
update defect_observations observation
set rating_tree_node_id = candidate.node_id
from defect_candidates candidate
where observation.id = candidate.observation_id
  and candidate.candidate_count = 1
  and observation.rating_tree_node_id is null;

-- 历史评定只补所用树的身份与摘要；结果 JSON、各级评分表和正式版本号均不改写。
update assessment_runs run
set
  rating_tree_version_id = profile.rating_tree_version_id,
  rating_tree_content_checksum = tree.tree_content_checksum
from project_standard_profiles profile
join rating_tree_versions tree on tree.id = profile.rating_tree_version_id
where run.standard_profile_id = profile.id
  and run.rating_tree_version_id is null;

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
    if current_setting(
      'bridge_report.allow_unbound_rating_tree_profile', true
    ) = 'on' then
      return new;
    end if;
    if tg_op = 'INSERT' or old.rating_tree_version_id is not null then
      raise exception using
        errcode = '23514',
        constraint = 'project_standard_profiles_rating_tree_required',
        message = 'new project standard profile requires a published rating tree';
    end if;
    -- 迁移前且无法唯一回填的历史 profile 只读保留，由诊断视图列出。
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
  if current_setting(
    'bridge_report.allow_unbound_rating_tree_defect', true
  ) = 'on' then
    return new;
  end if;
  select p.rating_tree_version_id into v_profile_tree_id
  from inspection_years iy
  join project_standard_profiles p on p.id = iy.standard_profile_id
  where iy.id = new.inspection_year_id;

  if v_profile_tree_id is null then
    -- 无法唯一迁移的历史年度仍可读取；新 profile 已无法进入该状态。
    return new;
  end if;
  if new.rating_tree_node_id is null then
    raise exception using
      errcode = '23514',
      constraint = 'defect_observations_rating_tree_required',
      message = 'defect observation requires a rating tree node';
  end if;

  select rating_tree_version_id, h21_indicator_id, is_selectable
    into v_node_tree_id, v_h21_indicator_id, v_selectable
  from rating_tree_nodes where id = new.rating_tree_node_id;
  if v_node_tree_id is distinct from v_profile_tree_id
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

create or replace function validate_assessment_run_rating_tree()
returns trigger
language plpgsql
as $$
declare
  v_profile_tree_id uuid;
  v_tree_checksum text;
begin
  if current_setting(
    'bridge_report.allow_unbound_rating_tree_run', true
  ) = 'on' then
    return new;
  end if;
  if new.standard_profile_id is null then
    return new;
  end if;
  select p.rating_tree_version_id, tree.tree_content_checksum
    into v_profile_tree_id, v_tree_checksum
  from project_standard_profiles p
  left join rating_tree_versions tree on tree.id = p.rating_tree_version_id
  where p.id = new.standard_profile_id;

  if v_profile_tree_id is null then
    -- 迁移前无法唯一绑定的历史 profile 不在数据库层伪造身份；
    -- 当前服务会在试算前阻断，且新 profile 已强制绑定。
    return new;
  end if;
  if new.rating_tree_version_id is distinct from v_profile_tree_id
     or new.rating_tree_content_checksum is distinct from v_tree_checksum then
    raise exception using
      errcode = '23514',
      constraint = 'assessment_runs_rating_tree_context_check',
      message = 'assessment run rating tree identity does not match its standard profile';
  end if;
  return new;
end
$$;

drop trigger if exists trg_assessment_runs_rating_tree on assessment_runs;
create trigger trg_assessment_runs_rating_tree
before insert or update of
  standard_profile_id, rating_tree_version_id, rating_tree_content_checksum
on assessment_runs
for each row execute function validate_assessment_run_rating_tree();

-- 精确列出仍需人工判断的数据，不删除、不自动选择多候选节点。
create or replace view rating_tree_binding_diagnostics as
select
  'project_standard_profile'::text as entity_type,
  p.id as entity_id,
  case
    when count(rtv.id) = 0 then 'no_published_tree'
    else 'multiple_published_trees'
  end::text as reason,
  count(rtv.id)::integer as candidate_count
from project_standard_profiles p
left join rating_tree_versions rtv
  on rtv.technical_condition_package_id = p.technical_condition_package_id
 and rtv.maintenance_package_id = p.maintenance_package_id
 and rtv.status = 'published'
where p.rating_tree_version_id is null
group by p.id
union all
select
  'defect_observation'::text,
  observation.id,
  case
    when count(node.id) = 0 then 'no_applicable_tree_node'
    else 'multiple_applicable_tree_nodes'
  end::text,
  count(node.id)::integer
from defect_observations observation
join inspection_years iy on iy.id = observation.inspection_year_id
join project_standard_profiles profile on profile.id = iy.standard_profile_id
left join bridge_component_inventory_entries entry
  on entry.inventory_revision_id = iy.component_inventory_revision_id
 and entry.bridge_component_id = observation.bridge_component_id
 and entry.is_active
left join bridge_component_standard_mappings mapping
  on mapping.inventory_entry_id = entry.id
 and mapping.standard_package_id = profile.technical_condition_package_id
 and mapping.is_active
 and mapping.confirmation_status = '已确认'
left join rating_tree_nodes node
  on node.rating_tree_version_id = profile.rating_tree_version_id
 and node.h21_indicator_id = observation.standard_defect_indicator_id
 and node.is_selectable
 and mapping.standard_bridge_type_id = any(node.bridge_type_ids)
 and mapping.standard_component_category_id = any(node.component_category_ids)
where profile.rating_tree_version_id is not null
  and observation.rating_tree_node_id is null
group by observation.id
union all
select
  'assessment_run'::text,
  run.id,
  'missing_rating_tree_identity'::text,
  0
from assessment_runs run
join project_standard_profiles profile on profile.id = run.standard_profile_id
where profile.rating_tree_version_id is not null
  and run.rating_tree_version_id is null;

comment on column assessment_runs.rating_tree_version_id is
  '该次试算或正式评定实际使用的不可变评定树版本；历史结果只补身份，不重算。';
comment on column assessment_runs.rating_tree_content_checksum is
  '评定运行时锁定的评定树内容 SHA-256，用于历史结果防漂移校验。';
comment on view rating_tree_binding_diagnostics is
  '无法安全自动回填的 profile、病害或评定运行精确清单；不得据此自动删除数据。';
