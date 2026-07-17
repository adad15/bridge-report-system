-- 011 冒烟测试：版本唯一性、条目和映射约束、确认后不可变、引用与级联。
-- 事务末尾回滚，不留下测试数据。

begin;

do $$
declare
  v_user_id uuid;
  v_bridge_id uuid;
  v_cascade_bridge_id uuid;
  v_package_id uuid;
  v_batch_id uuid;
  v_revision_id uuid;
  v_draft_revision_id uuid;
  v_formal_revision_id uuid;
  v_component_id uuid;
  v_draft_component_id uuid;
  v_entry_id uuid;
  v_draft_entry_id uuid;
  v_year_id uuid;
  v_violation_caught boolean;
begin
  insert into users (username, display_name, password_hash, role)
  values ('smoke_inventory_admin', '构件台账测试管理员', 'not-a-real-hash', 'admin')
  returning id into v_user_id;

  insert into standard_packages (
    standard_family, standard_id, standard_code, standard_name, official_edition,
    package_version, contract_version, algorithm_id, effective_date, content_checksum
  ) values (
    'technical_condition', 'SMOKE-INVENTORY-H21', 'SMOKE INVENTORY H21',
    '构件台账测试技术标准', '2026', '1.0.0', 1, 'smoke-inventory-h21',
    '2026-01-01', 'sha256:' || repeat('4', 64)
  ) returning id into v_package_id;

  insert into bridges (bridge_name) values ('011构件台账冒烟测试桥')
  returning id into v_bridge_id;

  insert into bridge_component_generation_batches (
    bridge_id, template_standard_package_id, template_id, bridge_type_code,
    input_quantities, generated_by_user_id
  ) values (
    v_bridge_id, v_package_id, 'h21.inventory.beam', 'h21.bridge_type.beam',
    '{"span_count":5,"girder_count_per_span":6}'::jsonb, v_user_id
  ) returning id into v_batch_id;

  insert into bridge_component_inventory_revisions (
    bridge_id, revision_number, created_by_user_id
  ) values (v_bridge_id, 1, v_user_id)
  returning id into v_revision_id;

  v_violation_caught := false;
  begin
    insert into bridge_component_inventory_revisions (
      bridge_id, revision_number, created_by_user_id
    ) values (v_bridge_id, 1, v_user_id);
  exception when unique_violation then
    v_violation_caught := true;
  end;
  if not v_violation_caught then
    raise exception '011 smoke: duplicate bridge revision number was not rejected';
  end if;

  insert into bridge_components (
    bridge_id, structure_part, component_type, business_component_code,
    normalized_component_key, creation_source
  ) values (
    v_bridge_id, '上部结构', '主梁', '1-1#', '011-main-girder-1-1', '人工录入'
  ) returning id into v_component_id;

  insert into bridge_component_inventory_entries (
    inventory_revision_id, bridge_component_id, generation_batch_id,
    component_number, site_name, site_component_type, span_or_location, sort_order
  ) values (
    v_revision_id, v_component_id, v_batch_id,
    '1-1#', '1-1#主梁', '主梁', '第1跨', 1
  ) returning id into v_entry_id;

  insert into bridge_component_standard_mappings (
    inventory_entry_id, standard_package_id, standard_bridge_type_id,
    standard_component_category_id, structure_part, mapping_source
  ) values (
    v_entry_id, v_package_id, 'h21.bridge_type.beam',
    'h21.component.beam.upper_bearing', 'superstructure', '模板生成'
  );

  v_violation_caught := false;
  begin
    insert into bridge_component_standard_mappings (
      inventory_entry_id, standard_package_id, standard_bridge_type_id,
      standard_component_category_id, structure_part, mapping_source
    ) values (
      v_entry_id, v_package_id, 'h21.bridge_type.beam',
      'h21.component.beam.upper_general', 'superstructure', '人工选择'
    );
  exception when unique_violation then
    v_violation_caught := true;
  end;
  if not v_violation_caught then
    raise exception '011 smoke: duplicate active entry/package mapping was not rejected';
  end if;

  insert into bridge_components (
    bridge_id, structure_part, component_type, business_component_code,
    normalized_component_key, creation_source
  ) values (
    v_bridge_id, '上部结构', '主梁', 'duplicate', '011-duplicate-component', '人工录入'
  ) returning id into v_draft_component_id;

  v_violation_caught := false;
  begin
    insert into bridge_component_inventory_entries (
      inventory_revision_id, bridge_component_id,
      component_number, site_name, site_component_type
    ) values (
      v_revision_id, v_draft_component_id, '1-1#', '重复主梁', '主梁'
    );
  exception when unique_violation then
    v_violation_caught := true;
  end;
  if not v_violation_caught then
    raise exception '011 smoke: duplicate site type and component number was not rejected';
  end if;

  update bridge_component_inventory_revisions
  set status = '已确认', confirmed_by_user_id = v_user_id, confirmed_at = now()
  where id = v_revision_id;

  v_violation_caught := false;
  begin
    update bridge_component_inventory_entries
    set component_number = '1-2#'
    where id = v_entry_id;
  exception when check_violation then
    v_violation_caught := true;
  end;
  if not v_violation_caught then
    raise exception '011 smoke: confirmed inventory entry was mutable';
  end if;

  insert into inspection_years (
    bridge_id, inspection_year, status, component_inventory_revision_id
  ) values (v_bridge_id, 2026, '已确认', v_revision_id)
  returning id into v_year_id;

  insert into defect_observations (
    inspection_year_id, bridge_id, bridge_component_id, structure_part,
    defect_type, defect_description_raw
  ) values (
    v_year_id, v_bridge_id, v_component_id, '上部结构', '裂缝', '011引用保护测试病害'
  );

  v_violation_caught := false;
  begin
    delete from bridge_components where id = v_component_id;
  exception when restrict_violation or foreign_key_violation then
    v_violation_caught := true;
  end;
  if not v_violation_caught then
    raise exception '011 smoke: defect-referenced physical component was deleted';
  end if;

  insert into bridge_component_inventory_revisions (
    bridge_id, revision_number, baseline_revision_id, created_by_user_id
  ) values (v_bridge_id, 2, v_revision_id, v_user_id)
  returning id into v_draft_revision_id;

  insert into bridge_components (
    bridge_id, structure_part, component_type, business_component_code,
    normalized_component_key, creation_source
  ) values (
    v_bridge_id, '上部结构', '主梁', '2-1#', '011-draft-component-2-1', '人工录入'
  ) returning id into v_draft_component_id;

  insert into bridge_component_inventory_entries (
    inventory_revision_id, bridge_component_id, component_number,
    site_name, site_component_type
  ) values (
    v_draft_revision_id, v_draft_component_id, '2-1#', '误生成主梁', '主梁'
  ) returning id into v_draft_entry_id;

  v_violation_caught := false;
  begin
    update bridge_component_inventory_entries
    set is_active = false
    where id = v_draft_entry_id;
  exception when check_violation then
    v_violation_caught := true;
  end;
  if not v_violation_caught then
    raise exception '011 smoke: deactivation without paired time and reason was accepted';
  end if;

  update bridge_component_inventory_entries
  set is_active = false, deactivated_at = now(), deactivation_reason = '现场确认不存在'
  where id = v_draft_entry_id;

  delete from bridge_components where id = v_draft_component_id;
  if exists (
    select 1 from bridge_component_inventory_entries where id = v_draft_entry_id
  ) then
    raise exception '011 smoke: unreferenced draft component entry did not cascade';
  end if;

  insert into bridge_component_inventory_revisions (
    bridge_id, revision_number, status, created_by_user_id,
    confirmed_by_user_id, confirmed_at
  ) values (
    v_bridge_id, 3, '已确认', v_user_id, v_user_id, now()
  ) returning id into v_formal_revision_id;

  update inspection_years
  set component_inventory_revision_id = v_formal_revision_id
  where id = v_year_id;

  v_violation_caught := false;
  begin
    delete from bridge_component_inventory_revisions where id = v_formal_revision_id;
  exception when restrict_violation or foreign_key_violation then
    v_violation_caught := true;
  end;
  if not v_violation_caught then
    raise exception '011 smoke: formally referenced inventory revision was deleted';
  end if;

  insert into bridges (bridge_name) values ('011级联删除测试桥')
  returning id into v_cascade_bridge_id;
  insert into bridge_component_inventory_revisions (
    bridge_id, revision_number, created_by_user_id
  ) values (v_cascade_bridge_id, 1, v_user_id)
  returning id into v_draft_revision_id;
  insert into bridge_components (
    bridge_id, structure_part, component_type, business_component_code,
    normalized_component_key, creation_source
  ) values (
    v_cascade_bridge_id, '桥面系', '桥面铺装', 'QM-1', '011-cascade-component', '人工录入'
  ) returning id into v_draft_component_id;
  insert into bridge_component_inventory_entries (
    inventory_revision_id, bridge_component_id, component_number,
    site_name, site_component_type
  ) values (
    v_draft_revision_id, v_draft_component_id, 'QM-1', '桥面铺装', '桥面铺装'
  ) returning id into v_draft_entry_id;
  insert into bridge_component_standard_mappings (
    inventory_entry_id, standard_package_id, standard_bridge_type_id,
    standard_component_category_id, structure_part, mapping_source
  ) values (
    v_draft_entry_id, v_package_id, 'h21.bridge_type.beam',
    'h21.component.deck.pavement', 'deck_system', '模板生成'
  );

  delete from bridges where id = v_cascade_bridge_id;
  if exists (
    select 1 from bridge_component_inventory_revisions where bridge_id = v_cascade_bridge_id
  ) or exists (
    select 1 from bridge_components where bridge_id = v_cascade_bridge_id
  ) or exists (
    select 1
    from bridge_component_inventory_entries e
    where e.id = v_draft_entry_id
  ) or exists (
    select 1
    from bridge_component_standard_mappings m
    where m.inventory_entry_id = v_draft_entry_id
  ) then
    raise exception '011 smoke: bridge inventory or mappings did not cascade';
  end if;

  raise notice '011 smoke passed';
end
$$;

rollback;
