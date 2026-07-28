-- 013 冒烟测试：系统评定运行、各级结果、控制结果、规则轨迹和正式结果保护。
-- 事务末尾回滚，不留下测试数据。

begin;
set local bridge_report.allow_unbound_rating_tree_profile = 'on';

do $$
declare
  v_user_id uuid;
  v_bridge_id uuid;
  v_technical_package_id uuid;
  v_maintenance_package_id uuid;
  v_profile_id uuid;
  v_inventory_revision_id uuid;
  v_draft_inventory_revision_id uuid;
  v_component_id uuid;
  v_inventory_entry_id uuid;
  v_year_id uuid;
  v_preview_run_id uuid;
  v_formal_run_id uuid;
  v_revised_run_id uuid;
  v_late_preview_run_id uuid;
  v_late_preview_trace_id uuid;
  v_trace_id uuid;
  v_violation_caught boolean;
begin
  insert into users (username, display_name, password_hash, role)
  values ('smoke_assessment_admin', '评定运行测试管理员', 'not-a-real-hash', 'admin')
  returning id into v_user_id;

  insert into standard_packages (
    standard_family, standard_id, standard_code, standard_name, official_edition,
    package_version, contract_version, algorithm_id, effective_date, content_checksum
  ) values (
    'technical_condition', 'SMOKE-ASSESSMENT-H21', 'SMOKE ASSESSMENT H21',
    '评定运行测试技术标准', '2026', '1.0.0', 1, 'smoke-assessment-h21',
    '2026-01-01', 'sha256:' || repeat('5', 64)
  ) returning id into v_technical_package_id;

  insert into standard_packages (
    standard_family, standard_id, standard_code, standard_name, official_edition,
    package_version, contract_version, algorithm_id, effective_date, content_checksum
  ) values (
    'maintenance', 'SMOKE-ASSESSMENT-5120', 'SMOKE ASSESSMENT 5120',
    '评定运行测试养护规范', '2026', '1.0.0', 1, 'smoke-assessment-5120',
    '2026-01-01', 'sha256:' || repeat('6', 64)
  ) returning id into v_maintenance_package_id;

  insert into project_standard_profiles (
    technical_condition_package_id, maintenance_package_id,
    created_by_user_id, change_reason
  ) values (
    v_technical_package_id, v_maintenance_package_id,
    v_user_id, '013评定运行冒烟测试'
  ) returning id into v_profile_id;

  insert into bridges (bridge_name) values ('013系统评定运行冒烟测试桥')
  returning id into v_bridge_id;

  insert into bridge_component_inventory_revisions (
    bridge_id, revision_number, created_by_user_id
  ) values (v_bridge_id, 1, v_user_id)
  returning id into v_inventory_revision_id;

  insert into bridge_components (
    bridge_id, structure_part, component_type, business_component_code,
    normalized_component_key, creation_source
  ) values (
    v_bridge_id, '上部结构', '主梁', '1-1#',
    '013-assessment-main-girder-1-1', '人工录入'
  ) returning id into v_component_id;

  insert into bridge_component_inventory_entries (
    inventory_revision_id, bridge_component_id, component_number,
    site_name, site_component_type, span_or_location, sort_order
  ) values (
    v_inventory_revision_id, v_component_id, '1-1#',
    '1-1#主梁', '主梁', '第1跨', 1
  ) returning id into v_inventory_entry_id;

  insert into bridge_component_standard_mappings (
    inventory_entry_id, standard_package_id, standard_bridge_type_id,
    standard_component_category_id, structure_part, mapping_source,
    confirmation_status, confirmed_by_user_id, confirmed_at
  ) values (
    v_inventory_entry_id, v_technical_package_id, 'h21.bridge_type.beam',
    'h21.component.beam.upper_bearing', 'superstructure', '人工选择',
    '已确认', v_user_id, now()
  );

  update bridge_component_inventory_revisions
  set status = '已确认', confirmed_by_user_id = v_user_id, confirmed_at = now()
  where id = v_inventory_revision_id;

  insert into bridge_component_inventory_revisions (
    bridge_id, revision_number, baseline_revision_id, created_by_user_id
  ) values (
    v_bridge_id, 2, v_inventory_revision_id, v_user_id
  ) returning id into v_draft_inventory_revision_id;

  insert into inspection_years (
    bridge_id, inspection_year, status, standard_profile_id,
    component_inventory_revision_id
  ) values (
    v_bridge_id, 2026, '待校对', v_profile_id, v_inventory_revision_id
  ) returning id into v_year_id;

  -- 试算允许不形成正式锁定，并可连同临时结果和轨迹一起清理。
  insert into assessment_runs (
    inspection_year_id, run_kind, result_status,
    input_summary_json, input_checksum,
    rule_package_summary_json, rule_package_checksum,
    result_summary_json, created_by_user_id
  ) values (
    v_year_id, '试算', '成功',
    '{"defect_count":1}'::jsonb, 'sha256:' || repeat('7', 64),
    '{"algorithm_id":"smoke-assessment-h21"}'::jsonb, 'sha256:' || repeat('8', 64),
    '{"overall_score":95}'::jsonb, v_user_id
  ) returning id into v_preview_run_id;

  insert into assessment_part_results (
    assessment_run_id, result_level, result_key, structure_part,
    score, grade, result_json
  ) values (
    v_preview_run_id, '全桥', 'overall', 'overall',
    95, '1类', '{"score":95}'::jsonb
  );

  insert into assessment_rule_traces (
    assessment_run_id, sequence_number, rule_id, trace_stage,
    target_type, target_key, input_json, output_json
  ) values (
    v_preview_run_id, 1, 'smoke.preview.overall', '汇总',
    '全桥', 'overall', '{"score":95}'::jsonb, '{"grade":"1类"}'::jsonb
  );

  delete from assessment_runs where id = v_preview_run_id;
  if exists (select 1 from assessment_part_results where assessment_run_id = v_preview_run_id)
     or exists (select 1 from assessment_rule_traces where assessment_run_id = v_preview_run_id) then
    raise exception '013 smoke: preview result cleanup did not cascade';
  end if;

  -- 正式运行必须同时锁定技术规范包、项目规范组合和已确认构件台账。
  v_violation_caught := false;
  begin
    insert into assessment_runs (
      inspection_year_id, run_kind, formal_revision_number, result_status,
      input_summary_json, input_checksum,
      rule_package_summary_json, rule_package_checksum,
      result_summary_json, created_by_user_id
    ) values (
      v_year_id, '正式', 1, '运行中',
      '{"defect_count":1}'::jsonb, 'sha256:' || repeat('9', 64),
      '{"algorithm_id":"missing-links"}'::jsonb, 'sha256:' || repeat('a', 64),
      '{}'::jsonb, v_user_id
    );
  exception when check_violation then
    v_violation_caught := true;
  end;
  if not v_violation_caught then
    raise exception '013 smoke: formal run without locked standards and inventory was accepted';
  end if;

  v_violation_caught := false;
  begin
    insert into assessment_runs (
      inspection_year_id, run_kind, formal_revision_number,
      technical_condition_package_id, standard_profile_id,
      component_inventory_revision_id, result_status,
      input_summary_json, input_checksum,
      rule_package_summary_json, rule_package_checksum,
      result_summary_json, created_by_user_id
    ) values (
      v_year_id, '正式', 1,
      v_technical_package_id, v_profile_id,
      v_inventory_revision_id, '运行中',
      '{"defect_count":1}'::jsonb, 'sha256:' || repeat('9', 64),
      '{"algorithm_id":"checksum-mismatch"}'::jsonb, 'sha256:' || repeat('a', 64),
      '{}'::jsonb, v_user_id
    );
  exception when check_violation then
    v_violation_caught := true;
  end;
  if not v_violation_caught then
    raise exception '013 smoke: mismatched rule package checksum was accepted';
  end if;

  v_violation_caught := false;
  begin
    insert into assessment_runs (
      inspection_year_id, run_kind, formal_revision_number,
      technical_condition_package_id, standard_profile_id,
      component_inventory_revision_id, result_status,
      input_summary_json, input_checksum,
      rule_package_summary_json, rule_package_checksum,
      result_summary_json, created_by_user_id
    ) values (
      v_year_id, '正式', 1,
      v_technical_package_id, v_profile_id,
      v_draft_inventory_revision_id, '运行中',
      '{"defect_count":1}'::jsonb, 'sha256:' || repeat('9', 64),
      '{"algorithm_id":"draft-inventory"}'::jsonb, 'sha256:' || repeat('5', 64),
      '{}'::jsonb, v_user_id
    );
  exception when check_violation then
    v_violation_caught := true;
  end;
  if not v_violation_caught then
    raise exception '013 smoke: formal run accepted an unconfirmed inventory revision';
  end if;

  -- 输入摘要和规则包摘要必须为非空对象。
  v_violation_caught := false;
  begin
    insert into assessment_runs (
      inspection_year_id, run_kind, result_status,
      input_summary_json, input_checksum,
      rule_package_summary_json, rule_package_checksum,
      result_summary_json, created_by_user_id
    ) values (
      v_year_id, '试算', '阻断',
      '{}'::jsonb, 'sha256:' || repeat('b', 64),
      '{"algorithm_id":"smoke"}'::jsonb, 'sha256:' || repeat('c', 64),
      '{"blocking_count":1}'::jsonb, v_user_id
    );
  exception when check_violation then
    v_violation_caught := true;
  end;
  if not v_violation_caught then
    raise exception '013 smoke: empty assessment input summary was accepted';
  end if;

  v_violation_caught := false;
  begin
    insert into assessment_runs (
      inspection_year_id, run_kind, result_status,
      input_summary_json, input_checksum,
      rule_package_summary_json, rule_package_checksum,
      result_summary_json, created_by_user_id
    ) values (
      v_year_id, '试算', '阻断',
      '{"defect_count":1}'::jsonb, 'sha256:' || repeat('b', 64),
      '{}'::jsonb, 'sha256:' || repeat('c', 64),
      '{"blocking_count":1}'::jsonb, v_user_id
    );
  exception when check_violation then
    v_violation_caught := true;
  end;
  if not v_violation_caught then
    raise exception '013 smoke: empty assessment rule package summary was accepted';
  end if;

  v_violation_caught := false;
  begin
    insert into assessment_runs (
      inspection_year_id, run_kind, result_status,
      input_summary_json, input_checksum,
      rule_package_summary_json, rule_package_checksum,
      result_summary_json, created_by_user_id
    ) values (
      v_year_id, '试算', null,
      '{"defect_count":1}'::jsonb, 'sha256:' || repeat('b', 64),
      '{"algorithm_id":"smoke"}'::jsonb, 'sha256:' || repeat('c', 64),
      '{"blocking_count":1}'::jsonb, v_user_id
    );
  exception when not_null_violation then
    v_violation_caught := true;
  end;
  if not v_violation_caught then
    raise exception '013 smoke: assessment run without result status was accepted';
  end if;

  insert into assessment_runs (
    inspection_year_id, run_kind, formal_revision_number,
    technical_condition_package_id, standard_profile_id,
    component_inventory_revision_id, result_status,
    input_summary_json, input_checksum,
    rule_package_summary_json, rule_package_checksum,
    result_summary_json, created_by_user_id
  ) values (
    v_year_id, '正式', 1,
    v_technical_package_id, v_profile_id, v_inventory_revision_id, '运行中',
    '{"defect_count":1,"component_count":1}'::jsonb,
    'sha256:' || repeat('d', 64),
    '{"algorithm_id":"smoke-assessment-h21","package_version":"1.0.0"}'::jsonb,
    'sha256:' || repeat('5', 64),
    '{}'::jsonb, v_user_id
  ) returning id into v_formal_run_id;

  insert into assessment_component_results (
    assessment_run_id, bridge_component_id, standard_component_category_id,
    structure_part, score, grade, deduction, result_json
  ) values (
    v_formal_run_id, v_component_id, 'h21.component.beam.upper_bearing',
    'superstructure', 92.5, '2类', 7.5, '{"score":92.5}'::jsonb
  );

  insert into assessment_part_results (
    assessment_run_id, result_level, result_key, structure_part,
    score, grade, weight, result_json
  ) values
    (v_formal_run_id, '部件', 'h21.component.beam.upper_bearing', 'superstructure',
     92.5, '2类', 1, '{"score":92.5}'::jsonb),
    (v_formal_run_id, '结构', 'superstructure', 'superstructure',
     92.5, '2类', 1, '{"score":92.5}'::jsonb),
    (v_formal_run_id, '全桥', 'overall', 'overall',
     92.5, '2类', 1, '{"score":92.5}'::jsonb);

  insert into assessment_control_results (
    assessment_run_id, rule_id, control_level, target_key,
    triggered, score_before, score_after, grade_before, grade_after,
    message, input_json, output_json
  ) values (
    v_formal_run_id, 'h21.single_control.smoke', '构件', v_component_id::text,
    false, 92.5, 92.5, '2类', '2类', '未触发单项控制',
    '{"scale":2}'::jsonb, '{"triggered":false}'::jsonb
  );

  insert into assessment_rule_traces (
    assessment_run_id, sequence_number, rule_id, trace_stage,
    target_type, target_key, input_json, output_json
  ) values (
    v_formal_run_id, 1, 'h21.component.score', '构件评分',
    '构件', v_component_id::text,
    '{"deductions":[7.5]}'::jsonb, '{"score":92.5}'::jsonb
  ) returning id into v_trace_id;

  -- 非有限值和越界分数必须被数据库拒绝。
  v_violation_caught := false;
  begin
    insert into assessment_part_results (
      assessment_run_id, result_level, result_key, structure_part, score, result_json
    ) values (
      v_formal_run_id, '部件', 'invalid-nan', 'superstructure',
      'NaN'::numeric, '{}'::jsonb
    );
  exception when check_violation then
    v_violation_caught := true;
  end;
  if not v_violation_caught then
    raise exception '013 smoke: non-finite assessment score was accepted';
  end if;

  v_violation_caught := false;
  begin
    insert into assessment_part_results (
      assessment_run_id, result_level, result_key, structure_part, score, result_json
    ) values (
      v_formal_run_id, '部件', 'invalid-overflow', 'superstructure',
      100.000001, '{}'::jsonb
    );
  exception when check_violation then
    v_violation_caught := true;
  end;
  if not v_violation_caught then
    raise exception '013 smoke: out-of-range assessment score was accepted';
  end if;

  -- 轨迹规则 ID 必填，输入和输出必须为 JSON 对象。
  v_violation_caught := false;
  begin
    insert into assessment_rule_traces (
      assessment_run_id, sequence_number, rule_id, trace_stage,
      target_type, target_key, input_json, output_json
    ) values (
      v_formal_run_id, 2, 'h21.invalid.trace', '测试',
      '构件', v_component_id::text, '[]'::jsonb, '{}'::jsonb
    );
  exception when check_violation then
    v_violation_caught := true;
  end;
  if not v_violation_caught then
    raise exception '013 smoke: non-object assessment trace input was accepted';
  end if;

  v_violation_caught := false;
  begin
    insert into assessment_rule_traces (
      assessment_run_id, sequence_number, rule_id, trace_stage,
      target_type, target_key, input_json, output_json
    ) values (
      v_formal_run_id, 2, '   ', '测试',
      '构件', v_component_id::text, '{}'::jsonb, '{}'::jsonb
    );
  exception when check_violation then
    v_violation_caught := true;
  end;
  if not v_violation_caught then
    raise exception '013 smoke: blank assessment trace rule id was accepted';
  end if;

  update assessment_runs
  set result_status = '成功', is_current = true,
      result_summary_json = '{"overall_score":92.5,"overall_grade":"2类"}'::jsonb,
      confirmed_by_user_id = v_user_id, confirmed_at = now()
  where id = v_formal_run_id;

  -- 同一年度不能同时存在两个当前正式评定。
  v_violation_caught := false;
  begin
    insert into assessment_runs (
      inspection_year_id, run_kind, formal_revision_number, supersedes_run_id,
      technical_condition_package_id, standard_profile_id,
      component_inventory_revision_id, result_status, is_current,
      input_summary_json, input_checksum,
      rule_package_summary_json, rule_package_checksum,
      result_summary_json, created_by_user_id,
      confirmed_by_user_id, confirmed_at
    ) values (
      v_year_id, '正式', 2, v_formal_run_id,
      v_technical_package_id, v_profile_id,
      v_inventory_revision_id, '成功', true,
      '{"defect_count":1}'::jsonb, 'sha256:' || repeat('f', 64),
      '{"algorithm_id":"smoke-assessment-h21"}'::jsonb, 'sha256:' || repeat('5', 64),
      '{"overall_score":91}'::jsonb, v_user_id,
      v_user_id, now()
    );
  exception when unique_violation then
    v_violation_caught := true;
  end;
  if not v_violation_caught then
    raise exception '013 smoke: two current formal assessment runs were accepted';
  end if;

  -- 修订时只把旧正式结果改为非当前，旧运行及其明细仍保留。
  update assessment_runs set is_current = false where id = v_formal_run_id;
  insert into assessment_runs (
    inspection_year_id, run_kind, formal_revision_number, supersedes_run_id,
    technical_condition_package_id, standard_profile_id,
    component_inventory_revision_id, result_status, is_current,
    input_summary_json, input_checksum,
    rule_package_summary_json, rule_package_checksum,
    result_summary_json, created_by_user_id,
    confirmed_by_user_id, confirmed_at
  ) values (
    v_year_id, '正式', 2, v_formal_run_id,
    v_technical_package_id, v_profile_id,
    v_inventory_revision_id, '成功', true,
    '{"defect_count":1}'::jsonb, 'sha256:' || repeat('1', 64),
    '{"algorithm_id":"smoke-assessment-h21"}'::jsonb, 'sha256:' || repeat('5', 64),
    '{"overall_score":91,"overall_grade":"2类"}'::jsonb, v_user_id,
    v_user_id, now()
  ) returning id into v_revised_run_id;

  if (select count(*) from assessment_runs where inspection_year_id = v_year_id and run_kind = '正式') <> 2
     or (select count(*) from assessment_runs where inspection_year_id = v_year_id and run_kind = '正式' and is_current) <> 1
     or not exists (select 1 from assessment_rule_traces where id = v_trace_id) then
    raise exception '013 smoke: formal assessment revision history was not preserved';
  end if;

  insert into condition_ratings (
    inspection_year_id, rating_level, structure_part, rating_item_name,
    score, grade, review_status, assessment_run_id
  ) values (
    v_year_id, '全桥', '全桥', '系统综合评定',
    91, '2类', '已确认', v_revised_run_id
  );

  -- 已成功的正式运行和结果明细均不可删除或改写。
  v_violation_caught := false;
  begin
    delete from assessment_rule_traces where id = v_trace_id;
  exception when check_violation then
    v_violation_caught := true;
  end;
  if not v_violation_caught then
    raise exception '013 smoke: trace of completed formal run was deleted';
  end if;

  insert into assessment_runs (
    inspection_year_id, run_kind, result_status,
    input_summary_json, input_checksum,
    rule_package_summary_json, rule_package_checksum,
    result_summary_json, created_by_user_id
  ) values (
    v_year_id, '试算', '运行中',
    '{"defect_count":1}'::jsonb, 'sha256:' || repeat('3', 64),
    '{"algorithm_id":"late-preview"}'::jsonb, 'sha256:' || repeat('4', 64),
    '{}'::jsonb, v_user_id
  ) returning id into v_late_preview_run_id;
  insert into assessment_rule_traces (
    assessment_run_id, sequence_number, rule_id, trace_stage,
    target_type, target_key, input_json, output_json
  ) values (
    v_late_preview_run_id, 99, 'smoke.preview.rebind', '测试',
    '全桥', 'overall', '{}'::jsonb, '{}'::jsonb
  ) returning id into v_late_preview_trace_id;

  v_violation_caught := false;
  begin
    update assessment_rule_traces
    set assessment_run_id = v_formal_run_id
    where id = v_late_preview_trace_id;
  exception when check_violation then
    v_violation_caught := true;
  end;
  if not v_violation_caught then
    raise exception '013 smoke: preview detail was moved into a completed formal run';
  end if;
  delete from assessment_runs where id = v_late_preview_run_id;

  v_violation_caught := false;
  begin
    delete from assessment_runs where id = v_formal_run_id;
  exception when check_violation or restrict_violation or foreign_key_violation then
    v_violation_caught := true;
  end;
  if not v_violation_caught then
    raise exception '013 smoke: completed formal assessment run was deleted';
  end if;

  -- 正式运行通过 restrict 引用保护规范包和已确认台账版本。
  v_violation_caught := false;
  begin
    delete from standard_packages where id = v_technical_package_id;
  exception when check_violation or restrict_violation or foreign_key_violation then
    v_violation_caught := true;
  end;
  if not v_violation_caught then
    raise exception '013 smoke: standard package referenced by formal run was deleted';
  end if;

  v_violation_caught := false;
  begin
    delete from bridge_component_inventory_revisions where id = v_inventory_revision_id;
  exception when check_violation or restrict_violation or foreign_key_violation then
    v_violation_caught := true;
  end;
  if not v_violation_caught then
    raise exception '013 smoke: inventory revision referenced by formal run was deleted';
  end if;

  raise notice '013 smoke passed';
end
$$;

rollback;
