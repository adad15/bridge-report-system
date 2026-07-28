begin;

do $$
declare
  v_user_id uuid;
  v_h21_id uuid;
  v_maintenance_id uuid;
  v_other_h21_id uuid;
  v_tree_id uuid;
  v_node_id uuid;
  v_profile_id uuid;
  v_bridge_id uuid;
  v_year_id uuid;
  v_component_id uuid;
  v_violation_caught boolean;
begin
  insert into users (username, display_name, password_hash, role)
  values ('rating-tree-smoke-' || gen_random_uuid()::text, '018评定树冒烟测试', 'not-used', 'admin')
  returning id into v_user_id;

  insert into standard_packages (
    standard_family, standard_id, standard_code, standard_name,
    official_edition, package_version, contract_version, algorithm_id,
    effective_date, content_checksum
  ) values (
    'technical_condition', 'h21-rating-tree-smoke', 'JTG/T H21',
    '公路桥梁技术状况评定标准', '2011', '1.0.1', 1, 'h21-smoke',
    '2026-01-01', 'sha256:' || repeat('1', 64)
  ) returning id into v_h21_id;

  insert into standard_packages (
    standard_family, standard_id, standard_code, standard_name,
    official_edition, package_version, contract_version, algorithm_id,
    effective_date, content_checksum
  ) values (
    'maintenance', 'jtg5120-rating-tree-smoke', 'JTG 5120',
    '公路桥涵养护规范', '2021', '1.0.0', 1, 'jtg5120-smoke',
    '2026-01-01', 'sha256:' || repeat('2', 64)
  ) returning id into v_maintenance_id;

  insert into standard_packages (
    standard_family, standard_id, standard_code, standard_name,
    official_edition, package_version, contract_version, algorithm_id,
    effective_date, content_checksum
  ) values (
    'technical_condition', 'other-h21-rating-tree-smoke', 'OTHER',
    '错误技术规范', '2026', '1.0.0', 1, 'other-smoke',
    '2026-01-01', 'sha256:' || repeat('3', 64)
  ) returning id into v_other_h21_id;

  insert into rating_tree_versions (
    tree_code, tree_name, package_version, contract_version,
    technical_condition_package_id, technical_condition_standard_id,
    technical_condition_package_version, technical_condition_content_checksum,
    maintenance_package_id, maintenance_standard_id,
    maintenance_package_version, maintenance_content_checksum,
    organization_tree_code, organization_package_version,
    organization_content_checksum, tree_content_checksum, status
  ) values (
    'organization-bridge-smoke', '单位桥梁评定树冒烟测试', '1.0.0', 1,
    v_h21_id, 'h21-rating-tree-smoke', '1.0.1', 'sha256:' || repeat('1', 64),
    v_maintenance_id, 'jtg5120-rating-tree-smoke', '1.0.0', 'sha256:' || repeat('2', 64),
    'organization-bridge-smoke', '1.0.0', 'sha256:' || repeat('4', 64),
    'sha256:' || repeat('5', 64), 'draft'
  ) returning id into v_tree_id;

  insert into rating_tree_nodes (
    rating_tree_version_id, node_key, display_name, node_type, sort_order,
    bridge_type_ids, component_category_ids, scoring_mode,
    h21_indicator_id, is_selectable, is_scoring,
    allowed_scales, detail_json
  ) values (
    v_tree_id, 'smoke.defect', '裂缝', 'defect', 1,
    array['beam'], array['h21.component.4_1_1'], 'inherit_h21',
    'h21.defect.4_1_1_1', true, true, array[1, 2, 3, 4, 5],
    '{"scale_descriptions":{"1":"完好"}}'::jsonb
  ) returning id into v_node_id;

  insert into rating_tree_node_sources (
    rating_tree_node_id, source_key, source_type, title, source_reference
  ) values (
    v_node_id, 'h21.defect.4_1_1_1', 'h21', 'H21 裂缝', '表 4.1.1-1'
  );

  insert into rating_tree_aliases (
    rating_tree_version_id, target_node_id, bridge_type_id,
    component_category_id, alias_text, normalized_alias
  ) values (
    v_tree_id, v_node_id, 'beam', 'h21.component.4_1_1', '梁体裂缝', '梁体裂缝'
  );

  update rating_tree_versions
  set status = 'published', published_at = now()
  where id = v_tree_id;

  insert into project_standard_profiles (
    technical_condition_package_id, maintenance_package_id,
    rating_tree_version_id, created_by_user_id, change_reason
  ) values (
    v_h21_id, v_maintenance_id, v_tree_id, v_user_id, '018冒烟测试规范组合'
  ) returning id into v_profile_id;

  v_violation_caught := false;
  begin
    insert into project_standard_profiles (
      technical_condition_package_id, maintenance_package_id,
      rating_tree_version_id, created_by_user_id, change_reason
    ) values (
      v_other_h21_id, v_maintenance_id, v_tree_id, v_user_id, '错误评定树组合'
    );
  exception when check_violation then
    v_violation_caught := true;
  end;
  if not v_violation_caught then
    raise exception '018 smoke: profile/tree package mismatch was not rejected';
  end if;

  v_violation_caught := false;
  begin
    update rating_tree_nodes set display_name = '被篡改' where id = v_node_id;
  exception when check_violation then
    v_violation_caught := true;
  end;
  if not v_violation_caught then
    raise exception '018 smoke: published tree node was mutable';
  end if;

  v_violation_caught := false;
  begin
    delete from rating_tree_aliases where rating_tree_version_id = v_tree_id;
  exception when check_violation then
    v_violation_caught := true;
  end;
  if not v_violation_caught then
    raise exception '018 smoke: published tree alias was mutable';
  end if;

  insert into bridges (bridge_name)
  values ('018评定树冒烟测试桥') returning id into v_bridge_id;
  insert into inspection_years (
    bridge_id, inspection_year, is_current, status, standard_profile_id
  ) values (
    v_bridge_id, 2098, false, '待校对', v_profile_id
  ) returning id into v_year_id;
  insert into bridge_components (
    bridge_id, structure_part, component_type, business_component_code,
    normalized_component_key
  ) values (
    v_bridge_id, '上部结构', '主梁', '018-1', '018-rating-tree-component'
  ) returning id into v_component_id;

  insert into defect_observations (
    inspection_year_id, bridge_id, bridge_component_id, structure_part,
    defect_type, defect_description_raw, standard_defect_indicator_id,
    rating_tree_node_id
  ) values (
    v_year_id, v_bridge_id, v_component_id, '上部结构',
    '裂缝', '冒烟测试裂缝', 'h21.defect.4_1_1_1', v_node_id
  );

  if not exists (
    select 1
    from pg_indexes
    where schemaname = 'public'
      and tablename = 'rating_tree_nodes'
      and indexname = 'ix_rating_tree_nodes_scoring_lookup'
  ) then
    raise exception '018 smoke: scoring lookup index is missing';
  end if;
end
$$;

rollback;
