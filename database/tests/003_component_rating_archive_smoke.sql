-- 003 冒烟测试：构件级 condition_ratings 校验列、唯一约束与 CHECK 约束。
-- 事务内构造数据并在末尾回滚，不在库中留下任何残留。

begin;

do $$
declare
  v_bridge_id uuid;
  v_inspection_id uuid;
  v_component_id uuid;
  v_rating_id uuid;
  v_violation_caught boolean;
begin
  insert into bridges (bridge_name) values ('003冒烟测试桥') returning id into v_bridge_id;

  insert into inspection_years (bridge_id, inspection_year, status)
  values (v_bridge_id, 2026, '已确认')
  returning id into v_inspection_id;

  insert into bridge_components (
    bridge_id, structure_part, component_type, business_component_code, normalized_component_key
  )
  values (v_bridge_id, '上部结构', '2-1#板', '上部承重构件', '上部结构|2-1#板|上部承重构件')
  returning id into v_component_id;

  -- 用例 1：构件级评分行携带完整三值校验信息可正常写入。
  insert into condition_ratings (
    inspection_year_id, rating_level, structure_part, bridge_component_id, rating_item_name,
    score, source_score, calculated_score, score_validation_status, score_resolution_reason,
    calculation_details_json, review_status
  )
  values (
    v_inspection_id, '构件', '上部结构', v_component_id, '2-1#板',
    55.81, 55.81, 55.8076118446, '一致', null,
    '{"standard":"JTG/T H21-2011 4.1.1","ordered_deductions":[35.0,20.0],"rounding_scale":2}'::jsonb,
    '已确认'
  )
  returning id into v_rating_id;

  if v_rating_id is null then
    raise exception '003 smoke: component rating insert failed';
  end if;

  -- 用例 2：同检测版本同构件第二条构件评分违反部分唯一索引。
  v_violation_caught := false;
  begin
    insert into condition_ratings (
      inspection_year_id, rating_level, structure_part, bridge_component_id, rating_item_name, score
    )
    values (v_inspection_id, '构件', '上部结构', v_component_id, '2-1#板', 60.00);
  exception when unique_violation then
    v_violation_caught := true;
  end;
  if not v_violation_caught then
    raise exception '003 smoke: duplicate component rating was not rejected';
  end if;

  -- 用例 3：构件级评分缺 bridge_component_id 违反 CHECK 约束。
  v_violation_caught := false;
  begin
    insert into condition_ratings (
      inspection_year_id, rating_level, structure_part, rating_item_name, score
    )
    values (v_inspection_id, '构件', '上部结构', '孤儿构件评分', 60.00);
  exception when check_violation then
    v_violation_caught := true;
  end;
  if not v_violation_caught then
    raise exception '003 smoke: component rating without bridge_component_id was not rejected';
  end if;

  -- 用例 4：非法 score_validation_status 违反 CHECK 约束。
  v_violation_caught := false;
  begin
    update condition_ratings set score_validation_status = '随便一个状态' where id = v_rating_id;
  exception when check_violation then
    v_violation_caught := true;
  end;
  if not v_violation_caught then
    raise exception '003 smoke: invalid score_validation_status was not rejected';
  end if;

  -- 用例 5：非构件层级不受构件绑定约束影响（历史 1.1 形态：新列全空）。
  insert into condition_ratings (
    inspection_year_id, rating_level, structure_part, rating_item_name, score, grade
  )
  values (v_inspection_id, '全桥', '全桥', '全桥', 85.61, '2类');

  raise notice '003 smoke passed';
end
$$;

rollback;
