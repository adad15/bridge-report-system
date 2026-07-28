begin;

do $$
declare
  v_user_id uuid;
  v_h21_id uuid;
  v_maintenance_id uuid;
  v_tree_id uuid;
  v_node_id uuid;
  v_profile_id uuid;
  v_bridge_id uuid;
  v_year_id uuid;
  v_component_id uuid;
  v_violation_caught boolean;
begin
  insert into users (username, display_name, password_hash, role)
  values (
    'rating-tree-required-smoke-' || gen_random_uuid()::text,
    '019评定树强制绑定冒烟测试', 'not-used', 'admin'
  ) returning id into v_user_id;

  insert into standard_packages (
    standard_family, standard_id, standard_code, standard_name,
    official_edition, package_version, contract_version, algorithm_id,
    effective_date, content_checksum
  ) values (
    'technical_condition', 'h21-required-smoke', 'JTG/T H21',
    '公路桥梁技术状况评定标准', '2011', '1.0.0', 1, 'h21-required-smoke',
    '2026-01-01', 'sha256:' || repeat('6', 64)
  ) returning id into v_h21_id;

  insert into standard_packages (
    standard_family, standard_id, standard_code, standard_name,
    official_edition, package_version, contract_version, algorithm_id,
    effective_date, content_checksum
  ) values (
    'maintenance', 'jtg5120-required-smoke', 'JTG 5120',
    '公路桥涵养护规范', '2021', '1.0.0', 1, 'jtg5120-required-smoke',
    '2026-01-01', 'sha256:' || repeat('7', 64)
  ) returning id into v_maintenance_id;

  insert into rating_tree_versions (
    tree_code, tree_name, package_version, contract_version,
    technical_condition_package_id, technical_condition_standard_id,
    technical_condition_package_version, technical_condition_content_checksum,
    maintenance_package_id, maintenance_standard_id,
    maintenance_package_version, maintenance_content_checksum,
    organization_tree_code, organization_package_version,
    organization_content_checksum, tree_content_checksum, status
  ) values (
    'organization-bridge-required-smoke', '单位桥梁评定树强制绑定测试', '1.0.0', 1,
    v_h21_id, 'h21-required-smoke', '1.0.0', 'sha256:' || repeat('6', 64),
    v_maintenance_id, 'jtg5120-required-smoke', '1.0.0', 'sha256:' || repeat('7', 64),
    'organization-bridge-required-smoke', '1.0.0', 'sha256:' || repeat('8', 64),
    'sha256:' || repeat('9', 64), 'draft'
  ) returning id into v_tree_id;

  insert into rating_tree_nodes (
    rating_tree_version_id, node_key, display_name, node_type, sort_order,
    bridge_type_ids, component_category_ids, scoring_mode,
    h21_indicator_id, is_selectable, is_scoring, allowed_scales
  ) values (
    v_tree_id, 'required.smoke.crack', '裂缝', 'defect', 1,
    array['beam'], array['h21.component.smoke'], 'inherit_h21',
    'h21.defect.required.smoke', true, true, array[1, 2, 3, 4, 5]
  ) returning id into v_node_id;

  update rating_tree_versions
  set status = 'published', published_at = now()
  where id = v_tree_id;

  v_violation_caught := false;
  begin
    insert into project_standard_profiles (
      technical_condition_package_id, maintenance_package_id,
      created_by_user_id, change_reason
    ) values (
      v_h21_id, v_maintenance_id, v_user_id, '缺少评定树，应被拒绝'
    );
  exception when check_violation then
    v_violation_caught := true;
  end;
  if not v_violation_caught then
    raise exception '019 smoke: new profile without rating tree was accepted';
  end if;

  insert into project_standard_profiles (
    technical_condition_package_id, maintenance_package_id,
    rating_tree_version_id, created_by_user_id, change_reason
  ) values (
    v_h21_id, v_maintenance_id, v_tree_id, v_user_id, '019冒烟测试规范组合'
  ) returning id into v_profile_id;

  insert into bridges (bridge_name)
  values ('019评定树强制绑定冒烟测试桥') returning id into v_bridge_id;
  insert into inspection_years (
    bridge_id, inspection_year, is_current, status, standard_profile_id
  ) values (
    v_bridge_id, 2097, false, '待校对', v_profile_id
  ) returning id into v_year_id;
  insert into bridge_components (
    bridge_id, structure_part, component_type, business_component_code,
    normalized_component_key
  ) values (
    v_bridge_id, '上部结构', '主梁', '019-1', '019-required-tree-component'
  ) returning id into v_component_id;

  v_violation_caught := false;
  begin
    insert into defect_observations (
      inspection_year_id, bridge_id, bridge_component_id, structure_part,
      defect_type, defect_description_raw, standard_defect_indicator_id
    ) values (
      v_year_id, v_bridge_id, v_component_id, '上部结构',
      '裂缝', '缺少树节点，应被拒绝', 'h21.defect.required.smoke'
    );
  exception when check_violation then
    v_violation_caught := true;
  end;
  if not v_violation_caught then
    raise exception '019 smoke: defect without rating tree node was accepted';
  end if;

  insert into defect_observations (
    inspection_year_id, bridge_id, bridge_component_id, structure_part,
    defect_type, defect_description_raw, standard_defect_indicator_id,
    rating_tree_node_id
  ) values (
    v_year_id, v_bridge_id, v_component_id, '上部结构',
    '裂缝', '具备树节点', 'h21.defect.required.smoke', v_node_id
  );

  v_violation_caught := false;
  begin
    insert into assessment_runs (
      inspection_year_id, run_kind, technical_condition_package_id,
      standard_profile_id, result_status,
      input_summary_json, input_checksum,
      rule_package_summary_json, rule_package_checksum,
      result_summary_json, created_by_user_id
    ) values (
      v_year_id, '试算', v_h21_id, v_profile_id, '阻断',
      '{"source":"019-smoke"}', 'sha256:' || repeat('a', 64),
      '{"package":"019-smoke"}', 'sha256:' || repeat('6', 64),
      '{}', v_user_id
    );
  exception when check_violation then
    v_violation_caught := true;
  end;
  if not v_violation_caught then
    raise exception '019 smoke: assessment run without rating tree identity was accepted';
  end if;

  insert into assessment_runs (
    inspection_year_id, run_kind, technical_condition_package_id,
    standard_profile_id, rating_tree_version_id, rating_tree_content_checksum,
    result_status, input_summary_json, input_checksum,
    rule_package_summary_json, rule_package_checksum,
    result_summary_json, created_by_user_id
  ) values (
    v_year_id, '试算', v_h21_id, v_profile_id,
    v_tree_id, 'sha256:' || repeat('9', 64), '阻断',
    '{"source":"019-smoke"}', 'sha256:' || repeat('a', 64),
    '{"package":"019-smoke"}', 'sha256:' || repeat('6', 64),
    '{}', v_user_id
  );

  if to_regclass('rating_tree_binding_diagnostics') is null then
    raise exception '019 smoke: rating tree diagnostics view is missing';
  end if;
end
$$;

rollback;
