-- 007 冒烟测试：整桥删除审计、桥梁文件清理队列及年度队列领取状态升级。
-- 事务末尾回滚，不留下测试数据。

begin;

do $$
declare
  v_bridge_id uuid;
  v_user_id uuid;
  v_bridge_audit_id uuid;
  v_year_audit_id uuid;
  v_inventory_package_id uuid;
  v_generation_batch_id uuid;
  v_inventory_revision_id uuid;
  v_inventory_component_id uuid;
  v_inventory_entry_id uuid;
  v_violation_caught boolean;
  v_bridge_fk uuid;
begin
  insert into bridges (bridge_name, route_number, station_mark)
  values ('007整桥删除冒烟测试桥', 'S213', 'K123+456')
  returning id into v_bridge_id;

  insert into users (username, display_name, password_hash, role)
  values ('smoke_bridge_delete_admin', '整桥删除测试管理员', 'not-a-real-hash', 'admin')
  returning id into v_user_id;

  insert into standard_packages (
    standard_family, standard_id, standard_code, standard_name, official_edition,
    package_version, contract_version, algorithm_id, effective_date, content_checksum
  ) values (
    'technical_condition', 'SMOKE-007-INVENTORY', 'SMOKE 007 INVENTORY',
    '整桥删除构件台账测试标准', '2026', '1.0.0', 1, 'smoke-007-inventory',
    '2026-01-01', 'sha256:' || repeat('7', 64)
  ) returning id into v_inventory_package_id;

  insert into bridge_component_generation_batches (
    bridge_id, template_standard_package_id, template_id, bridge_type_code,
    input_quantities, generated_by_user_id
  ) values (
    v_bridge_id, v_inventory_package_id, 'smoke-007-template', 'smoke.bridge.beam',
    '{"span_count":1}'::jsonb, v_user_id
  ) returning id into v_generation_batch_id;

  insert into bridge_component_inventory_revisions (
    bridge_id, revision_number, created_by_user_id
  ) values (v_bridge_id, 1, v_user_id)
  returning id into v_inventory_revision_id;

  insert into bridge_components (
    bridge_id, structure_part, component_type, business_component_code,
    normalized_component_key, creation_source
  ) values (
    v_bridge_id, '上部结构', '主梁', '1-1#', '007-delete-inventory-component', '人工录入'
  ) returning id into v_inventory_component_id;

  insert into bridge_component_inventory_entries (
    inventory_revision_id, bridge_component_id, generation_batch_id,
    component_number, site_name, site_component_type
  ) values (
    v_inventory_revision_id, v_inventory_component_id, v_generation_batch_id,
    '1-1#', '1-1#主梁', '主梁'
  ) returning id into v_inventory_entry_id;

  insert into bridge_component_standard_mappings (
    inventory_entry_id, standard_package_id, standard_bridge_type_id,
    standard_component_category_id, structure_part, mapping_source
  ) values (
    v_inventory_entry_id, v_inventory_package_id, 'smoke.bridge.beam',
    'smoke.component.main_girder', 'superstructure', '模板生成'
  );

  update bridge_component_inventory_revisions
  set status = '已确认', confirmed_by_user_id = v_user_id, confirmed_at = now()
  where id = v_inventory_revision_id;

  insert into bridge_deletion_audits (
    batch_id, bridge_id, bridge_system_number_snapshot, bridge_name_snapshot,
    route_number_snapshot, route_name_snapshot, station_mark_snapshot, status_snapshot,
    actor_user_id, actor_username_snapshot, actor_display_name_snapshot, reason,
    impact_json, deleted_counts_json
  )
  select gen_random_uuid(), id, system_number, bridge_name,
         route_number, route_name, station_mark, status,
         v_user_id, 'smoke_bridge_delete_admin', '整桥删除测试管理员', '误建测试桥',
         '{"counts":{"inspection_versions":2}}'::jsonb,
         '{"inspection_versions":2}'::jsonb
  from bridges where id = v_bridge_id
  returning id into v_bridge_audit_id;

  insert into bridge_archived_file_deletion_queue (
    bridge_deletion_audit_id, storage_relative_path
  ) values (
    v_bridge_audit_id, 'bridges/QL-000001/2026/report.docx'
  );

  update bridge_archived_file_deletion_queue
  set status = '清理中', processing_started_at = now()
  where bridge_deletion_audit_id = v_bridge_audit_id;

  update bridge_archived_file_deletion_queue
  set status = '失败待重试', attempt_count = 1, last_error = 'file busy',
      next_attempt_at = now() + interval '5 minutes', processing_started_at = null
  where bridge_deletion_audit_id = v_bridge_audit_id;

  update bridge_archived_file_deletion_queue
  set status = '已完成', attempt_count = 2, last_error = null,
      processing_started_at = null, completed_at = now()
  where bridge_deletion_audit_id = v_bridge_audit_id;

  v_violation_caught := false;
  begin
    insert into bridge_archived_file_deletion_queue (
      bridge_deletion_audit_id, storage_relative_path, status
    ) values (
      v_bridge_audit_id, 'bridges/QL-000001/2026/invalid.docx', '清理中'
    );
  exception when check_violation then
    v_violation_caught := true;
  end;
  if not v_violation_caught then
    raise exception '007 smoke: cleaning row without processing timestamp was not rejected';
  end if;

  v_violation_caught := false;
  begin
    insert into bridge_archived_file_deletion_queue (
      bridge_deletion_audit_id, storage_relative_path
    ) values (
      v_bridge_audit_id, '../outside.docx'
    );
  exception when check_violation then
    v_violation_caught := true;
  end;
  if not v_violation_caught then
    raise exception '007 smoke: unsafe bridge cleanup path was not rejected';
  end if;

  insert into inspection_year_deletion_audits (
    bridge_id, bridge_system_number_snapshot, bridge_name_snapshot, inspection_year,
    actor_user_id, actor_username_snapshot, actor_display_name_snapshot, reason
  )
  select id, system_number, bridge_name, 2026,
         v_user_id, 'smoke_bridge_delete_admin', '整桥删除测试管理员', '年度清理领取升级'
  from bridges where id = v_bridge_id
  returning id into v_year_audit_id;

  insert into archived_file_deletion_queue (
    deletion_audit_id, storage_relative_path, status, processing_started_at
  ) values (
    v_year_audit_id, 'bridges/QL-000001/2026/annual.docx', '清理中', now()
  );

  delete from archived_file_deletion_queue where deletion_audit_id = v_year_audit_id;
  delete from inspection_year_deletion_audits where id = v_year_audit_id;
  delete from bridges where id = v_bridge_id;

  if exists (
    select 1 from bridge_component_generation_batches where id = v_generation_batch_id
  ) or exists (
    select 1 from bridge_component_inventory_revisions where id = v_inventory_revision_id
  ) or exists (
    select 1 from bridge_components where id = v_inventory_component_id
  ) or exists (
    select 1 from bridge_component_inventory_entries where id = v_inventory_entry_id
  ) or exists (
    select 1 from bridge_component_standard_mappings
    where inventory_entry_id = v_inventory_entry_id
  ) then
    raise exception '007 smoke: bridge inventory hierarchy was not deleted';
  end if;

  select bridge_id into v_bridge_fk
  from bridge_deletion_audits where id = v_bridge_audit_id;
  if v_bridge_fk is not null then
    raise exception '007 smoke: bridge deletion audit foreign key was not cleared';
  end if;

  if not exists (
    select 1 from bridge_deletion_audits
    where id = v_bridge_audit_id
      and bridge_system_number_snapshot is not null
      and bridge_name_snapshot = '007整桥删除冒烟测试桥'
  ) then
    raise exception '007 smoke: bridge deletion audit snapshot was not retained';
  end if;

  raise notice '007 smoke passed';
end
$$;

rollback;
