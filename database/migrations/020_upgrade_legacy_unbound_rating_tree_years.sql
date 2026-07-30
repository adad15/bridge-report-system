begin;

-- 评定树成为必选上下文前创建的待校对年度，可能仍锁定一个已经停用、
-- 且没有对应已发布评定树的旧 H21 包。只迁移尚无成功正式评定、台账映射
-- 已完整确认的数据；旧规范组合、旧台账和既有试算记录全部保留。
create temporary table legacy_rating_tree_year_upgrades on commit drop as
select
  iy.id as inspection_year_id,
  iy.standard_profile_id as old_profile_id,
  iy.component_inventory_revision_id as old_inventory_revision_id,
  profile.technical_condition_package_id as old_technical_package_id,
  target.id as target_tree_id,
  target.technical_condition_package_id as target_technical_package_id,
  target.maintenance_package_id as target_maintenance_package_id,
  null::uuid as new_profile_id,
  null::uuid as new_inventory_revision_id
from inspection_years iy
join project_standard_profiles profile
  on profile.id = iy.standard_profile_id
 and profile.rating_tree_version_id is null
join standard_packages old_technical
  on old_technical.id = profile.technical_condition_package_id
join standard_packages old_maintenance
  on old_maintenance.id = profile.maintenance_package_id
join bridge_component_inventory_revisions old_inventory
  on old_inventory.id = iy.component_inventory_revision_id
 and old_inventory.bridge_id = iy.bridge_id
 and old_inventory.status = '已确认'
join lateral (
  select tree.*
  from rating_tree_versions tree
  join standard_packages target_technical
    on target_technical.id = tree.technical_condition_package_id
   and target_technical.standard_id = old_technical.standard_id
   and target_technical.is_enabled
   and target_technical.sync_status = '正常'
  join standard_packages target_maintenance
    on target_maintenance.id = tree.maintenance_package_id
   and target_maintenance.standard_id = old_maintenance.standard_id
   and target_maintenance.is_enabled
   and target_maintenance.sync_status = '正常'
  where tree.status = 'published'
  order by tree.published_at desc, tree.package_version desc, tree.id
  limit 1
) target on true
where iy.status = '待校对'
  and not exists (
    select 1
    from assessment_runs run
    where run.inspection_year_id = iy.id
      and run.run_kind = '正式'
      and run.result_status = '成功'
  )
  and not exists (
    select 1
    from bridge_component_inventory_entries entry
    where entry.inventory_revision_id = iy.component_inventory_revision_id
      and entry.is_active
      and not exists (
        select 1
        from bridge_component_standard_mappings mapping
        where mapping.inventory_entry_id = entry.id
          and mapping.standard_package_id =
              profile.technical_condition_package_id
          and mapping.is_active
          and mapping.confirmation_status = '已确认'
      )
  );

-- 规范组合本身不覆盖：为目标评定树复用或创建一个新的生效组合。
insert into project_standard_profiles (
  technical_condition_package_id,
  maintenance_package_id,
  rating_tree_version_id,
  created_by_user_id,
  change_reason
)
select distinct on (upgrade.target_tree_id)
  upgrade.target_technical_package_id,
  upgrade.target_maintenance_package_id,
  upgrade.target_tree_id,
  old_profile.created_by_user_id,
  '评定树必选升级：为旧待校对年度创建兼容规范组合'
from legacy_rating_tree_year_upgrades upgrade
join project_standard_profiles old_profile
  on old_profile.id = upgrade.old_profile_id
where not exists (
  select 1
  from project_standard_profiles existing
  where existing.rating_tree_version_id = upgrade.target_tree_id
    and existing.status = '生效'
)
order by upgrade.target_tree_id, old_profile.created_at, old_profile.id;

update legacy_rating_tree_year_upgrades upgrade
set new_profile_id = (
  select profile.id
  from project_standard_profiles profile
  where profile.rating_tree_version_id = upgrade.target_tree_id
    and profile.status = '生效'
  order by profile.created_at, profile.id
  limit 1
);

-- 已确认台账不可原地改映射。创建下一版台账，保留构件身份和人工确认的
-- 桥型/构件类别，只把映射的规则包身份升级到目标评定树所用 H21 包。
create temporary table legacy_rating_tree_inventory_upgrades on commit drop as
with distinct_targets as (
  select distinct
    upgrade.old_inventory_revision_id,
    upgrade.old_technical_package_id,
    upgrade.target_technical_package_id
  from legacy_rating_tree_year_upgrades upgrade
),
upgrade_bases as (
  select
    target.*,
    old_inventory.bridge_id,
    old_inventory.created_by_user_id,
    old_inventory.confirmed_by_user_id,
    (
      select coalesce(max(existing_revision.revision_number), 0)
      from bridge_component_inventory_revisions existing_revision
      where existing_revision.bridge_id = old_inventory.bridge_id
    ) as current_max_revision_number
  from distinct_targets target
  join bridge_component_inventory_revisions old_inventory
    on old_inventory.id = target.old_inventory_revision_id
),
numbered as (
  select
    upgrade_base.*,
    upgrade_base.current_max_revision_number + row_number() over (
      partition by upgrade_base.bridge_id
      order by upgrade_base.old_inventory_revision_id,
               upgrade_base.target_technical_package_id
    ) as new_revision_number
  from upgrade_bases upgrade_base
)
select
  gen_random_uuid() as new_inventory_revision_id,
  old_inventory_revision_id,
  old_technical_package_id,
  target_technical_package_id,
  bridge_id,
  created_by_user_id,
  confirmed_by_user_id,
  new_revision_number
from numbered;

insert into bridge_component_inventory_revisions (
  id,
  bridge_id,
  revision_number,
  baseline_revision_id,
  created_by_user_id
)
select
  upgrade.new_inventory_revision_id,
  upgrade.bridge_id,
  upgrade.new_revision_number,
  upgrade.old_inventory_revision_id,
  upgrade.created_by_user_id
from legacy_rating_tree_inventory_upgrades upgrade;

insert into bridge_component_inventory_entries (
  inventory_revision_id,
  bridge_component_id,
  generation_batch_id,
  component_number,
  site_name,
  site_component_type,
  span_or_location,
  is_active,
  deactivated_at,
  deactivation_reason,
  sort_order,
  remarks
)
select
  upgrade.new_inventory_revision_id,
  old_entry.bridge_component_id,
  old_entry.generation_batch_id,
  old_entry.component_number,
  old_entry.site_name,
  old_entry.site_component_type,
  old_entry.span_or_location,
  old_entry.is_active,
  old_entry.deactivated_at,
  old_entry.deactivation_reason,
  old_entry.sort_order,
  old_entry.remarks
from legacy_rating_tree_inventory_upgrades upgrade
join bridge_component_inventory_entries old_entry
  on old_entry.inventory_revision_id = upgrade.old_inventory_revision_id;

insert into bridge_component_standard_mappings (
  inventory_entry_id,
  standard_package_id,
  standard_bridge_type_id,
  standard_component_category_id,
  structure_part,
  mapping_source,
  confirmation_status,
  confirmed_by_user_id,
  confirmed_at,
  is_active
)
select
  new_entry.id,
  upgrade.target_technical_package_id,
  old_mapping.standard_bridge_type_id,
  old_mapping.standard_component_category_id,
  old_mapping.structure_part,
  '规则包升级继承',
  old_mapping.confirmation_status,
  old_mapping.confirmed_by_user_id,
  old_mapping.confirmed_at,
  old_mapping.is_active
from legacy_rating_tree_inventory_upgrades upgrade
join bridge_component_inventory_entries old_entry
  on old_entry.inventory_revision_id = upgrade.old_inventory_revision_id
join bridge_component_inventory_entries new_entry
  on new_entry.inventory_revision_id = upgrade.new_inventory_revision_id
 and new_entry.bridge_component_id = old_entry.bridge_component_id
join bridge_component_standard_mappings old_mapping
  on old_mapping.inventory_entry_id = old_entry.id
 and old_mapping.standard_package_id = upgrade.old_technical_package_id
 and old_mapping.is_active;

update bridge_component_inventory_revisions revision
set
  status = '已确认',
  confirmed_by_user_id = upgrade.confirmed_by_user_id,
  confirmed_at = now(),
  confirmation_note = '评定树必选升级：继承原台账构件及已确认规范映射',
  updated_at = now()
from legacy_rating_tree_inventory_upgrades upgrade
where revision.id = upgrade.new_inventory_revision_id
  and upgrade.confirmed_by_user_id is not null
  and not exists (
    select 1
    from bridge_component_inventory_entries entry
    where entry.inventory_revision_id = upgrade.new_inventory_revision_id
      and entry.is_active
      and not exists (
        select 1
        from bridge_component_standard_mappings mapping
        where mapping.inventory_entry_id = entry.id
          and mapping.standard_package_id =
              upgrade.target_technical_package_id
          and mapping.is_active
          and mapping.confirmation_status = '已确认'
      )
  );

update legacy_rating_tree_year_upgrades year_upgrade
set new_inventory_revision_id = inventory_upgrade.new_inventory_revision_id
from legacy_rating_tree_inventory_upgrades inventory_upgrade
where inventory_upgrade.old_inventory_revision_id =
        year_upgrade.old_inventory_revision_id
  and inventory_upgrade.old_technical_package_id =
        year_upgrade.old_technical_package_id
  and inventory_upgrade.target_technical_package_id =
        year_upgrade.target_technical_package_id;

update inspection_years year
set
  standard_profile_id = upgrade.new_profile_id,
  component_inventory_revision_id = upgrade.new_inventory_revision_id,
  updated_at = now()
from legacy_rating_tree_year_upgrades upgrade
join bridge_component_inventory_revisions new_inventory
  on new_inventory.id = upgrade.new_inventory_revision_id
 and new_inventory.status = '已确认'
where year.id = upgrade.inspection_year_id
  and upgrade.new_profile_id is not null;

-- 已入档病害只在当前树中存在唯一适用节点时自动补齐；多候选或无候选仍留给人工。
with defect_candidates as (
  select
    observation.id as observation_id,
    (array_agg(node.id order by node.sort_order, node.node_key))[1] as node_id,
    count(*) as candidate_count
  from legacy_rating_tree_year_upgrades upgrade
  join defect_observations observation
    on observation.inspection_year_id = upgrade.inspection_year_id
   and observation.rating_tree_node_id is null
   and observation.standard_defect_indicator_id is not null
  join bridge_component_inventory_entries entry
    on entry.inventory_revision_id = upgrade.new_inventory_revision_id
   and entry.bridge_component_id = observation.bridge_component_id
   and entry.is_active
  join bridge_component_standard_mappings mapping
    on mapping.inventory_entry_id = entry.id
   and mapping.standard_package_id =
       upgrade.target_technical_package_id
   and mapping.is_active
   and mapping.confirmation_status = '已确认'
  join rating_tree_nodes node
    on node.rating_tree_version_id = upgrade.target_tree_id
   and node.h21_indicator_id = observation.standard_defect_indicator_id
   and node.is_selectable
   and mapping.standard_bridge_type_id = any(node.bridge_type_ids)
   and mapping.standard_component_category_id = any(node.component_category_ids)
  group by observation.id
)
update defect_observations observation
set rating_tree_node_id = candidate.node_id,
    updated_at = now()
from defect_candidates candidate
where observation.id = candidate.observation_id
  and candidate.candidate_count = 1
  and observation.rating_tree_node_id is null;

commit;
