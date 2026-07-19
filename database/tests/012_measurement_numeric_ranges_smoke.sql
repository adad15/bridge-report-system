-- 012 冒烟测试：单值、区间端点、约数以及互斥约束。
begin;

do $$
declare
  v_bridge_id uuid;
  v_year_id uuid;
  v_component_id uuid;
  v_observation_id uuid;
  v_violation_caught boolean;
begin
  insert into bridges (bridge_name) values ('012尺寸区间冒烟测试桥') returning id into v_bridge_id;
  insert into inspection_years (bridge_id, inspection_year) values (v_bridge_id, 2026) returning id into v_year_id;
  insert into bridge_components (
    bridge_id, structure_part, component_type, business_component_code, normalized_component_key
  ) values (
    v_bridge_id, '上部结构', '主梁', '1-1#', '012-range-main-girder'
  ) returning id into v_component_id;
  insert into defect_observations (
    inspection_year_id, bridge_id, bridge_component_id, structure_part, defect_type, defect_description_raw
  ) values (
    v_year_id, v_bridge_id, v_component_id, '上部结构', '裂缝', '012尺寸区间测试病害'
  ) returning id into v_observation_id;

  insert into defect_measurements (
    defect_observation_id, measurement_type, value_type, numeric_value, unit, raw_text, is_approximate
  ) values (
    v_observation_id, '长度', 'single', 20.0, 'm', '长度20.0m', false
  );
  insert into defect_measurements (
    defect_observation_id, measurement_type, value_type, minimum_value, maximum_value, unit, raw_text, is_approximate
  ) values (
    v_observation_id, '长度', 'range', 0.5, 4.0, 'm', '约0.5~4.0m', true
  );

  if not exists (
    select 1 from defect_measurements
    where defect_observation_id = v_observation_id and value_type = 'range'
      and minimum_value = 0.5 and maximum_value = 4.0 and is_approximate
  ) then
    raise exception '012 smoke: range endpoints or approximate flag were not preserved';
  end if;

  v_violation_caught := false;
  begin
    insert into defect_measurements (
      defect_observation_id, measurement_type, value_type, minimum_value, maximum_value, unit, raw_text
    ) values (v_observation_id, '长度', 'range', 4.0, 0.5, 'm', '4.0~0.5m');
  exception when check_violation then
    v_violation_caught := true;
  end;
  if not v_violation_caught then
    raise exception '012 smoke: reversed range was accepted';
  end if;

  v_violation_caught := false;
  begin
    insert into defect_measurements (
      defect_observation_id, measurement_type, value_type, numeric_value, minimum_value, unit, raw_text
    ) values (v_observation_id, '长度', 'single', 1.0, 0.5, 'm', '1.0m');
  exception when check_violation then
    v_violation_caught := true;
  end;
  if not v_violation_caught then
    raise exception '012 smoke: mixed single/range values were accepted';
  end if;

  raise notice '012 smoke passed';
end
$$;

rollback;
