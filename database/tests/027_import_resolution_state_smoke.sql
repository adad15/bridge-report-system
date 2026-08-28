-- 027 冒烟测试：构件解析/评分树解析关系表的身份、状态不变量、覆盖白名单与级联。
-- 事务末尾回滚，不留下测试数据。

begin;

do $$
declare
  v_user_id uuid;
  v_bridge_id uuid;
  v_other_bridge_id uuid;
  v_package_id uuid;
  v_revision_id uuid;
  v_import_id uuid;
  v_component_a_id uuid;
  v_component_b_id uuid;
  v_foreign_component_id uuid;
  v_group_id uuid;
  v_blank_group_id uuid;
  v_member_a_id uuid;
  v_member_b_id uuid;
  v_target_a_id uuid;
  v_target_b_id uuid;
  v_instance_id uuid;
  v_instance_b_id uuid;
  v_tree_version_id uuid;
  v_violation_caught boolean;
begin
  insert into users (username, display_name, password_hash, role)
  values ('smoke_resolution_admin', '解析状态测试管理员', 'not-a-real-hash', 'admin')
  returning id into v_user_id;

  insert into standard_packages (
    standard_family, standard_id, standard_code, standard_name, official_edition,
    package_version, contract_version, algorithm_id, effective_date, content_checksum
  ) values (
    'technical_condition', 'SMOKE-RESOLUTION-H21', 'SMOKE RESOLUTION H21',
    '解析状态测试技术标准', '2026', '1.0.0', 1, 'smoke-resolution-h21',
    '2026-01-01', 'sha256:' || repeat('a', 64)
  ) returning id into v_package_id;

  insert into bridges (bridge_name) values ('027解析状态冒烟测试桥')
  returning id into v_bridge_id;
  insert into bridges (bridge_name) values ('027解析状态冒烟测试邻桥')
  returning id into v_other_bridge_id;

  insert into bridge_component_inventory_revisions (
    bridge_id, revision_number, created_by_user_id
  ) values (v_bridge_id, 1, v_user_id)
  returning id into v_revision_id;

  insert into bridge_components (
    bridge_id, structure_part, component_type, business_component_code,
    normalized_component_key
  ) values
    (v_bridge_id, '上部结构', '板', '1-1#板', '027-smoke-1-1'),
    (v_bridge_id, '上部结构', '板', '1-2#板', '027-smoke-1-2');
  select id into v_component_a_id from bridge_components
   where bridge_id = v_bridge_id and business_component_code = '1-1#板';
  select id into v_component_b_id from bridge_components
   where bridge_id = v_bridge_id and business_component_code = '1-2#板';

  insert into bridge_components (
    bridge_id, structure_part, component_type, business_component_code,
    normalized_component_key
  ) values (v_other_bridge_id, '上部结构', '板', '1-1#板', '027-smoke-other-1-1')
  returning id into v_foreign_component_id;

  insert into bridge_component_inventory_entries (
    inventory_revision_id, bridge_component_id, component_number,
    site_name, site_component_type
  ) values
    (v_revision_id, v_component_a_id, '1-1#板', '027解析状态冒烟测试桥', '板'),
    (v_revision_id, v_component_b_id, '1-2#板', '027解析状态冒烟测试桥', '板');

  insert into import_records (bridge_id, import_name, source_type)
  values (v_bridge_id, '027解析状态冒烟导入', '接口同步')
  returning id into v_import_id;

  -- 8.0：来源草稿版本从 1 起，且不允许倒退到 0。
  if (select draft_version from import_records where id = v_import_id) <> 1 then
    raise exception '027 smoke: draft_version did not default to 1';
  end if;

  v_violation_caught := false;
  begin
    update import_records set draft_version = 0 where id = v_import_id;
  exception when check_violation then
    v_violation_caught := true;
  end;
  if not v_violation_caught then
    raise exception '027 smoke: draft_version accepted a non-positive value';
  end if;

  -- 8.1：bound 组必须钉住台账版本。
  v_violation_caught := false;
  begin
    insert into import_component_resolution_groups (
      import_record_id, source_component_name, normalized_component_number, status
    ) values (v_import_id, '板', '1-1', 'bound');
  exception when check_violation then
    v_violation_caught := true;
  end;
  if not v_violation_caught then
    raise exception '027 smoke: a bound group was accepted without an inventory revision';
  end if;

  insert into import_component_resolution_groups (
    import_record_id, source_component_name, normalized_component_number,
    status, match_method, inventory_revision_id, resolved_by_user_id, resolved_at
  ) values (
    v_import_id, '板', '1-1', 'bound', 'exact', v_revision_id, v_user_id, now()
  ) returning id into v_group_id;

  -- 8.1：同一导入内组身份唯一。
  v_violation_caught := false;
  begin
    insert into import_component_resolution_groups (
      import_record_id, source_component_name, normalized_component_number
    ) values (v_import_id, '板', '1-1');
  exception when unique_violation then
    v_violation_caught := true;
  end;
  if not v_violation_caught then
    raise exception '027 smoke: duplicate group identity was accepted';
  end if;

  -- 8.1：无编号病害落空串。写 NULL 会让唯一约束静默失效，这里必须真的挡住第二条。
  insert into import_component_resolution_groups (
    import_record_id, source_component_name, normalized_component_number
  ) values (v_import_id, '桥面铺装', '')
  returning id into v_blank_group_id;

  v_violation_caught := false;
  begin
    insert into import_component_resolution_groups (
      import_record_id, source_component_name, normalized_component_number
    ) values (v_import_id, '桥面铺装', '');
  exception when unique_violation then
    v_violation_caught := true;
  end;
  if not v_violation_caught then
    raise exception '027 smoke: a second unnumbered defect formed its own group';
  end if;

  v_violation_caught := false;
  begin
    insert into import_component_resolution_groups (
      import_record_id, source_component_name, normalized_component_number
    ) values (v_import_id, '桥面铺装', null);
  exception when not_null_violation then
    v_violation_caught := true;
  end;
  if not v_violation_caught then
    raise exception '027 smoke: normalized_component_number accepted null';
  end if;

  -- 8.2：一条来源病害在同一导入内只能属于一个组。
  insert into import_component_group_members (
    import_record_id, group_id, source_candidate_id, source_order
  ) values (v_import_id, v_group_id, 'source_defect_0001', 0)
  returning id into v_member_a_id;

  insert into import_component_group_members (
    import_record_id, group_id, source_candidate_id, source_order
  ) values (v_import_id, v_group_id, 'source_defect_0002', 1)
  returning id into v_member_b_id;

  v_violation_caught := false;
  begin
    insert into import_component_group_members (
      import_record_id, group_id, source_candidate_id, source_order
    ) values (v_import_id, v_blank_group_id, 'source_defect_0001', 0);
  exception when unique_violation then
    v_violation_caught := true;
  end;
  if not v_violation_caught then
    raise exception '027 smoke: one source defect joined two groups';
  end if;

  -- 8.2：成员与组必须同属一个导入记录（复合外键）。
  v_violation_caught := false;
  begin
    insert into import_component_group_members (
      import_record_id, group_id, source_candidate_id, source_order
    ) values (
      (select id from import_records where id <> v_import_id limit 1),
      v_group_id, 'source_defect_9999', 0
    );
  exception when foreign_key_violation or not_null_violation then
    v_violation_caught := true;
  end;
  if not v_violation_caught then
    raise exception '027 smoke: a member crossed into another import record';
  end if;

  -- 8.3：目标不能跨桥。
  v_violation_caught := false;
  begin
    insert into import_component_resolution_targets (
      group_id, bridge_component_id, target_order
    ) values (v_group_id, v_foreign_component_id, 1);
  exception when check_violation then
    v_violation_caught := true;
  end;
  if not v_violation_caught then
    raise exception '027 smoke: a resolution target crossed to another bridge';
  end if;

  insert into import_component_resolution_targets (
    group_id, bridge_component_id, target_order, target_role
  ) values (v_group_id, v_component_a_id, 1, 'range_member')
  returning id into v_target_a_id;

  insert into import_component_resolution_targets (
    group_id, bridge_component_id, target_order, target_role
  ) values (v_group_id, v_component_b_id, 2, 'range_member')
  returning id into v_target_b_id;

  -- 8.3：同一组不能重复选择同一构件。
  v_violation_caught := false;
  begin
    insert into import_component_resolution_targets (
      group_id, bridge_component_id, target_order
    ) values (v_group_id, v_component_a_id, 3);
  exception when unique_violation then
    v_violation_caught := true;
  end;
  if not v_violation_caught then
    raise exception '027 smoke: the same component was selected twice in one group';
  end if;

  -- 8.4：一条来源病害在一个目标上只有一个实例。
  insert into import_resolved_defect_instances (
    group_member_id, target_id, instance_order, component_resolution_version,
    is_photo_owner
  ) values (v_member_a_id, v_target_a_id, 1, 1, true)
  returning id into v_instance_id;

  insert into import_resolved_defect_instances (
    group_member_id, target_id, instance_order, component_resolution_version
  ) values (v_member_a_id, v_target_b_id, 2, 1)
  returning id into v_instance_b_id;

  v_violation_caught := false;
  begin
    insert into import_resolved_defect_instances (
      group_member_id, target_id, instance_order, component_resolution_version
    ) values (v_member_a_id, v_target_a_id, 3, 1);
  exception when unique_violation then
    v_violation_caught := true;
  end;
  if not v_violation_caught then
    raise exception '027 smoke: one source defect got two instances on one target';
  end if;

  -- 8.4：一条来源病害至多一个照片归属者。
  v_violation_caught := false;
  begin
    update import_resolved_defect_instances
       set is_photo_owner = true where id = v_instance_b_id;
  exception when unique_violation then
    v_violation_caught := true;
  end;
  if not v_violation_caught then
    raise exception '027 smoke: two instances of one source defect owned the photos';
  end if;

  -- 8.4：被忽略的实例不能持有照片。
  v_violation_caught := false;
  begin
    update import_resolved_defect_instances
       set instance_status = 'ignored' where id = v_instance_id;
  exception when check_violation then
    v_violation_caught := true;
  end;
  if not v_violation_caught then
    raise exception '027 smoke: an ignored instance kept photo ownership';
  end if;

  -- 8.4：覆盖白名单——未知字段。
  v_violation_caught := false;
  begin
    update import_resolved_defect_instances
       set fact_overrides_json = '{"bridge_component_id":"x"}'::jsonb
     where id = v_instance_b_id;
  exception when check_violation then
    v_violation_caught := true;
  end;
  if not v_violation_caught then
    raise exception '027 smoke: fact overrides accepted a resolution field';
  end if;

  -- 8.4：必填事实不能借覆盖写成 null。
  v_violation_caught := false;
  begin
    update import_resolved_defect_instances
       set fact_overrides_json = '{"defect_type":null}'::jsonb
     where id = v_instance_b_id;
  exception when check_violation then
    v_violation_caught := true;
  end;
  if not v_violation_caught then
    raise exception '027 smoke: fact overrides nulled a required defect fact';
  end if;

  -- 8.4：标度必须是正整数或 null。
  v_violation_caught := false;
  begin
    update import_resolved_defect_instances
       set fact_overrides_json = '{"defect_scale":0}'::jsonb
     where id = v_instance_b_id;
  exception when check_violation then
    v_violation_caught := true;
  end;
  if not v_violation_caught then
    raise exception '027 smoke: fact overrides accepted a non-positive scale';
  end if;

  update import_resolved_defect_instances
     set fact_overrides_json =
       '{"defect_scale":3,"remark":null,"defect_location":"底板","measurements":[]}'::jsonb
   where id = v_instance_b_id;

  -- 8.5：matched 必须带节点；unresolved 不得留节点或标准指标。
  select id into v_tree_version_id from rating_tree_versions
   where status = 'published' order by created_at limit 1;
  if v_tree_version_id is null then
    select id into v_tree_version_id from rating_tree_versions order by created_at limit 1;
  end if;

  if v_tree_version_id is not null then
    v_violation_caught := false;
    begin
      insert into import_rating_resolutions (
        resolved_defect_instance_id, rating_tree_version_id, status,
        component_resolution_version, applicability_hash, match_input_hash
      ) values (
        v_instance_id, v_tree_version_id, 'matched', 1,
        'sha256:' || repeat('b', 64), 'sha256:' || repeat('c', 64)
      );
    exception when check_violation then
      v_violation_caught := true;
    end;
    if not v_violation_caught then
      raise exception '027 smoke: a matched rating resolution was stored without a node';
    end if;

    insert into import_rating_resolutions (
      resolved_defect_instance_id, rating_tree_version_id, status,
      component_resolution_version, applicability_hash, match_input_hash
    ) values (
      v_instance_id, v_tree_version_id, 'unresolved', 1,
      'sha256:' || repeat('b', 64), 'sha256:' || repeat('c', 64)
    );

    -- 主键即实例外键：每个实例至多一个当前评分树解析。
    v_violation_caught := false;
    begin
      insert into import_rating_resolutions (
        resolved_defect_instance_id, rating_tree_version_id, status,
        component_resolution_version, applicability_hash, match_input_hash
      ) values (
        v_instance_id, v_tree_version_id, 'unresolved', 1,
        'sha256:' || repeat('d', 64), 'sha256:' || repeat('e', 64)
      );
    exception when unique_violation then
      v_violation_caught := true;
    end;
    if not v_violation_caught then
      raise exception '027 smoke: an instance kept two rating resolutions';
    end if;

    v_violation_caught := false;
    begin
      update import_rating_resolutions
         set standard_defect_indicator_id = 'h21-indicator-1'
       where resolved_defect_instance_id = v_instance_id;
    exception when check_violation then
      v_violation_caught := true;
    end;
    if not v_violation_caught then
      raise exception '027 smoke: an unresolved rating resolution kept a standard indicator';
    end if;
  end if;

  -- 8.6：applied 计划必须留下可重放的首次结果。
  v_violation_caught := false;
  begin
    insert into import_resolution_operation_plans (
      import_record_id, actor_user_id, operation_type, lock_token_hash,
      request_json, plan_json, preconditions_json, status, expires_at
    ) values (
      v_import_id, v_user_id, 'bulk_replace', 'sha256:' || repeat('f', 64),
      '{}'::jsonb, '{}'::jsonb, '{}'::jsonb, 'applied', now() + interval '15 minutes'
    );
  exception when check_violation then
    v_violation_caught := true;
  end;
  if not v_violation_caught then
    raise exception '027 smoke: an applied plan was stored without a replayable result';
  end if;

  insert into import_resolution_operation_plans (
    import_record_id, actor_user_id, operation_type, lock_token_hash,
    request_json, plan_json, preconditions_json, expires_at
  ) values (
    v_import_id, v_user_id, 'inventory_repoint', 'sha256:' || repeat('f', 64),
    '{}'::jsonb, '{}'::jsonb, '{}'::jsonb, now() + interval '15 minutes'
  );

  insert into import_resolution_reopen_snapshots (
    import_record_id, snapshot_json, checksum, created_by_user_id
  ) values (v_import_id, '{}'::jsonb, 'sha256:' || repeat('1', 64), v_user_id);

  insert into import_resolution_events (
    import_record_id, group_id, operation_type, actor_user_id
  ) values (v_import_id, v_group_id, 'bind', v_user_id);

  -- 删除导入记录必须带走全部解析状态、计划、审计与重开快照。
  delete from import_records where id = v_import_id;

  if exists (select 1 from import_component_resolution_groups where import_record_id = v_import_id)
     or exists (select 1 from import_component_group_members where import_record_id = v_import_id)
     or exists (select 1 from import_component_resolution_targets where group_id = v_group_id)
     or exists (select 1 from import_resolved_defect_instances where id = v_instance_id)
     or exists (select 1 from import_rating_resolutions where resolved_defect_instance_id = v_instance_id)
     or exists (select 1 from import_resolution_operation_plans where import_record_id = v_import_id)
     or exists (select 1 from import_resolution_events where import_record_id = v_import_id)
     or exists (select 1 from import_resolution_reopen_snapshots where import_record_id = v_import_id)
  then
    raise exception '027 smoke: deleting the import record left resolution state behind';
  end if;
end
$$;

-- 组状态与目标集合的一致性是延迟约束：它只在事务提交时才检查，因此单独开一个
-- 子事务验证，而不是混在上面那段里。
do $$
declare
  v_user_id uuid;
  v_bridge_id uuid;
  v_import_id uuid;
  v_revision_id uuid;
  v_component_id uuid;
  v_group_id uuid;
  v_violation_caught boolean := false;
begin
  insert into users (username, display_name, password_hash, role)
  values ('smoke_resolution_deferred', '解析状态延迟约束测试', 'not-a-real-hash', 'admin')
  returning id into v_user_id;

  insert into bridges (bridge_name) values ('027解析状态延迟约束测试桥')
  returning id into v_bridge_id;

  insert into bridge_component_inventory_revisions (
    bridge_id, revision_number, created_by_user_id
  ) values (v_bridge_id, 1, v_user_id) returning id into v_revision_id;

  insert into bridge_components (
    bridge_id, structure_part, component_type, business_component_code,
    normalized_component_key
  ) values (v_bridge_id, '上部结构', '板', '1-1#板', '027-deferred-1-1')
  returning id into v_component_id;

  insert into bridge_component_inventory_entries (
    inventory_revision_id, bridge_component_id, component_number,
    site_name, site_component_type
  ) values (v_revision_id, v_component_id, '1-1#板', '027解析状态延迟约束测试桥', '板');

  insert into import_records (bridge_id, import_name, source_type)
  values (v_bridge_id, '027延迟约束冒烟导入', '接口同步')
  returning id into v_import_id;

  -- bound 但一个目标都不写。延迟约束只在事务提交时检查，plpgsql 的子块提交不算，
  -- 所以用 set constraints all immediate 把待检的约束就地逼出来。
  begin
    insert into import_component_resolution_groups (
      import_record_id, source_component_name, normalized_component_number,
      status, match_method, inventory_revision_id
    ) values (v_import_id, '板', '1-1', 'bound', 'exact', v_revision_id)
    returning id into v_group_id;
    set constraints all immediate;
  exception when check_violation then
    v_violation_caught := true;
  end;
  set constraints all deferred;
  if not v_violation_caught then
    raise exception '027 smoke: a bound group survived with no resolution target';
  end if;

  -- 正向：先写组、再写目标，同一事务内按顺序落地必须通过。
  insert into import_component_resolution_groups (
    import_record_id, source_component_name, normalized_component_number,
    status, match_method, inventory_revision_id
  ) values (v_import_id, '板', '1-1', 'bound', 'exact', v_revision_id)
  returning id into v_group_id;
  insert into import_component_resolution_targets (
    group_id, bridge_component_id, target_order
  ) values (v_group_id, v_component_id, 1);
  set constraints all immediate;
  set constraints all deferred;

  -- 反向：unresolved 组挂着目标同样不合法。
  v_violation_caught := false;
  begin
    insert into import_component_resolution_groups (
      import_record_id, source_component_name, normalized_component_number,
      status, inventory_revision_id
    ) values (v_import_id, '板', '2-1', 'unresolved', v_revision_id)
    returning id into v_group_id;
    insert into import_component_resolution_targets (
      group_id, bridge_component_id, target_order
    ) values (v_group_id, v_component_id, 1);
    set constraints all immediate;
  exception when check_violation then
    v_violation_caught := true;
  end;
  set constraints all deferred;
  if not v_violation_caught then
    raise exception '027 smoke: an unresolved group kept a resolution target';
  end if;

  raise notice '027 smoke passed';
end
$$;

rollback;
