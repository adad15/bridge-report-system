-- 021 冒烟：受控关键词规则只能挂在同版本的可选择节点上，且随已发布版本一同冻结。
begin;

do $$
declare
  v_h21_id uuid;
  v_maintenance_id uuid;
  v_tree_id uuid;
  v_node_id uuid;
  v_group_id uuid;
  v_rule_id uuid;
  v_violation_caught boolean;
begin
  insert into standard_packages (
    standard_family, standard_id, standard_code, standard_name,
    official_edition, package_version, contract_version, algorithm_id,
    effective_date, content_checksum
  ) values (
    'technical_condition', 'h21-keyword-rule-smoke', 'JTG/T H21',
    '公路桥梁技术状况评定标准', '2011', '1.0.1', 1, 'h21-keyword-smoke',
    '2026-01-01', 'sha256:' || repeat('6', 64)
  ) returning id into v_h21_id;

  insert into standard_packages (
    standard_family, standard_id, standard_code, standard_name,
    official_edition, package_version, contract_version, algorithm_id,
    effective_date, content_checksum
  ) values (
    'maintenance', 'jtg5120-keyword-rule-smoke', 'JTG 5120',
    '公路桥涵养护规范', '2021', '1.0.0', 1, 'jtg5120-keyword-smoke',
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
    'organization-bridge-keyword-smoke', '受控关键词冒烟测试树', '1.0.0', 1,
    v_h21_id, 'h21-keyword-rule-smoke', '1.0.1', 'sha256:' || repeat('6', 64),
    v_maintenance_id, 'jtg5120-keyword-rule-smoke', '1.0.0', 'sha256:' || repeat('7', 64),
    'organization-bridge-keyword-smoke', '1.0.0', 'sha256:' || repeat('8', 64),
    'sha256:' || repeat('9', 64), 'draft'
  ) returning id into v_tree_id;

  insert into rating_tree_nodes (
    rating_tree_version_id, node_key, display_name, node_type, sort_order,
    bridge_type_ids, component_category_ids, scoring_mode,
    h21_indicator_id, is_selectable, is_scoring, allowed_scales, detail_json
  ) values (
    v_tree_id, 'smoke.keyword.water', '水损', 'defect', 1,
    array['beam'], array['h21.component.beam.upper_general'], 'reference_h21',
    'h21.defect.5_1_1_6', true, true, array[1, 2, 3, 4], '{}'::jsonb
  ) returning id into v_node_id;

  insert into rating_tree_nodes (
    rating_tree_version_id, node_key, display_name, node_type, sort_order,
    bridge_type_ids, component_category_ids, scoring_mode,
    is_selectable, is_scoring, allowed_scales, detail_json
  ) values (
    v_tree_id, 'smoke.keyword.group', '上部结构', 'component_group', 0,
    array['beam'], array['h21.component.beam.upper_general'], 'non_scoring',
    false, false, array[]::integer[], '{}'::jsonb
  ) returning id into v_group_id;

  insert into rating_tree_keyword_rules (
    rating_tree_version_id, target_node_id, rule_key, bridge_type_id,
    component_category_id, positive_keywords, excluded_keywords,
    auto_bind, sort_order, rule_note
  ) values (
    v_tree_id, v_node_id, 'smoke.rule.water', 'beam',
    'h21.component.beam.upper_general', array['渗水'], array['泄水管'],
    true, 10, '冒烟测试规则'
  ) returning id into v_rule_id;

  -- 目标必须是可选择的病害节点。
  v_violation_caught := false;
  begin
    insert into rating_tree_keyword_rules (
      rating_tree_version_id, target_node_id, rule_key, bridge_type_id,
      component_category_id, positive_keywords
    ) values (
      v_tree_id, v_group_id, 'smoke.rule.group', 'beam',
      'h21.component.beam.upper_general', array['渗水']
    );
  exception when check_violation then
    v_violation_caught := true;
  end;
  if not v_violation_caught then
    raise exception '021 smoke: keyword rule targeting an unselectable node was accepted';
  end if;

  -- 规则适用范围不能超出目标节点自身的桥型与构件范围。
  v_violation_caught := false;
  begin
    insert into rating_tree_keyword_rules (
      rating_tree_version_id, target_node_id, rule_key, bridge_type_id,
      component_category_id, positive_keywords
    ) values (
      v_tree_id, v_node_id, 'smoke.rule.out-of-scope', 'beam',
      'h21.component.deck.pavement', array['渗水']
    );
  exception when check_violation then
    v_violation_caught := true;
  end;
  if not v_violation_caught then
    raise exception '021 smoke: keyword rule outside its target scope was accepted';
  end if;

  -- 正向关键词不能为空。
  v_violation_caught := false;
  begin
    insert into rating_tree_keyword_rules (
      rating_tree_version_id, target_node_id, rule_key, bridge_type_id,
      component_category_id, positive_keywords
    ) values (
      v_tree_id, v_node_id, 'smoke.rule.empty', 'beam',
      'h21.component.beam.upper_general', array[]::text[]
    );
  exception when check_violation then
    v_violation_caught := true;
  end;
  if not v_violation_caught then
    raise exception '021 smoke: keyword rule without positive keywords was accepted';
  end if;

  update rating_tree_versions
  set status = 'published', published_at = now()
  where id = v_tree_id;

  -- 发布后规则冻结：页面运行时不可编辑。
  v_violation_caught := false;
  begin
    update rating_tree_keyword_rules set auto_bind = false where id = v_rule_id;
  exception when check_violation then
    v_violation_caught := true;
  end;
  if not v_violation_caught then
    raise exception '021 smoke: published keyword rule was mutable';
  end if;

  v_violation_caught := false;
  begin
    delete from rating_tree_keyword_rules where id = v_rule_id;
  exception when check_violation then
    v_violation_caught := true;
  end;
  if not v_violation_caught then
    raise exception '021 smoke: published keyword rule was deletable';
  end if;

  -- 018 的节点与别名冻结不能因为 021 重定义触发器函数而失效。
  v_violation_caught := false;
  begin
    update rating_tree_nodes set display_name = '被篡改' where id = v_node_id;
  exception when check_violation then
    v_violation_caught := true;
  end;
  if not v_violation_caught then
    raise exception '021 smoke: published tree node became mutable again';
  end if;

  if not exists (
    select 1 from pg_indexes
    where tablename = 'rating_tree_keyword_rules'
      and indexname = 'ix_rating_tree_keyword_rules_scope'
  ) then
    raise exception '021 smoke: keyword rule scope index is missing';
  end if;
end
$$;

rollback;
