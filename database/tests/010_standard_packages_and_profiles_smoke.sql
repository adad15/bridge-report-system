-- 010 冒烟测试：包身份摘要、family、组合不可变、继承与引用保护。
begin;
set local bridge_report.allow_unbound_rating_tree_profile = 'on';

do $$
declare
  v_user_id uuid;
  v_bridge_id uuid;
  v_source_year_id uuid;
  v_revision_year_id uuid;
  v_unbound_year_id uuid;
  v_technical_id uuid;
  v_maintenance_id uuid;
  v_other_technical_id uuid;
  v_profile_id uuid;
  v_violation_caught boolean;
begin
  insert into users (username, display_name, password_hash, role)
  values ('smoke_standard_admin', '规范组合测试管理员', 'not-a-real-hash', 'admin')
  returning id into v_user_id;

  insert into standard_packages (
    standard_family, standard_id, standard_code, standard_name, official_edition,
    package_version, contract_version, algorithm_id, effective_date, content_checksum
  ) values
    ('technical_condition', 'SMOKE-H21', 'SMOKE H21', '测试技术评定标准', '2026',
     '1.0.0', 1, 'smoke-h21', '2026-01-01', 'sha256:' || repeat('1', 64))
  returning id into v_technical_id;

  insert into standard_packages (
    standard_family, standard_id, standard_code, standard_name, official_edition,
    package_version, contract_version, algorithm_id, effective_date, content_checksum
  ) values
    ('maintenance', 'SMOKE-5120', 'SMOKE 5120', '测试养护规范', '2026',
     '1.0.0', 1, 'smoke-maintenance', '2026-01-01', 'sha256:' || repeat('2', 64))
  returning id into v_maintenance_id;

  insert into standard_packages (
    standard_family, standard_id, standard_code, standard_name, official_edition,
    package_version, contract_version, algorithm_id, effective_date, content_checksum
  ) values
    ('technical_condition', 'SMOKE-OTHER', 'SMOKE OTHER', '另一测试技术标准', '2026',
     '1.0.0', 1, 'smoke-other', '2026-01-01', 'sha256:' || repeat('3', 64))
  returning id into v_other_technical_id;

  v_violation_caught := false;
  begin
    insert into standard_packages (
      standard_family, standard_id, standard_code, standard_name, official_edition,
      package_version, contract_version, algorithm_id, effective_date, content_checksum
    ) values
      ('technical_condition', 'SMOKE-H21', 'SMOKE H21', '摘要冲突', '2026',
       '1.0.0', 1, 'smoke-h21', '2026-01-01', 'sha256:' || repeat('9', 64));
  exception when unique_violation then
    v_violation_caught := true;
  end;
  if not v_violation_caught then
    raise exception '010 smoke: identity/version checksum conflict was not rejected';
  end if;

  v_violation_caught := false;
  begin
    update standard_packages
    set content_checksum = 'sha256:' || repeat('8', 64)
    where id = v_technical_id;
  exception when check_violation then
    v_violation_caught := true;
  end;
  if not v_violation_caught then
    raise exception '010 smoke: installed package identity was mutable';
  end if;

  insert into project_standard_profiles (
    technical_condition_package_id, maintenance_package_id, created_by_user_id, change_reason
  ) values (v_technical_id, v_maintenance_id, v_user_id, '010冒烟测试初始组合')
  returning id into v_profile_id;

  v_violation_caught := false;
  begin
    insert into project_standard_profiles (
      technical_condition_package_id, maintenance_package_id, created_by_user_id, change_reason
    ) values (v_technical_id, v_other_technical_id, v_user_id, '错误family组合');
  exception when check_violation then
    v_violation_caught := true;
  end;
  if not v_violation_caught then
    raise exception '010 smoke: package family mismatch was not rejected';
  end if;

  insert into bridges (bridge_name) values ('010规范组合冒烟测试桥') returning id into v_bridge_id;
  insert into inspection_years (
    bridge_id, inspection_year, is_current, status
  ) values (v_bridge_id, 2025, false, '待校对')
  returning id into v_unbound_year_id;
  insert into inspection_years (
    bridge_id, inspection_year, status, standard_profile_id
  ) values (v_bridge_id, 2026, '已确认', v_profile_id)
  returning id into v_source_year_id;
  insert into inspection_years (
    bridge_id, inspection_year, version_number, is_current,
    revision_source_inspection_id, status, standard_profile_id
  ) values (
    v_bridge_id, 2026, 2, false, v_source_year_id, '待校对', v_profile_id
  ) returning id into v_revision_year_id;

  if not exists (
    select 1 from inspection_years
    where id = v_revision_year_id and standard_profile_id = v_profile_id
  ) then
    raise exception '010 smoke: revision did not inherit profile';
  end if;

  v_violation_caught := false;
  begin
    update project_standard_profiles
    set technical_condition_package_id = v_other_technical_id
    where id = v_profile_id;
  exception when check_violation then
    v_violation_caught := true;
  end;
  if not v_violation_caught then
    raise exception '010 smoke: formal profile was mutable';
  end if;

  v_violation_caught := false;
  begin
    delete from standard_packages where id = v_technical_id;
  exception when restrict_violation or foreign_key_violation then
    v_violation_caught := true;
  end;
  if not v_violation_caught then
    raise exception '010 smoke: referenced package deletion was not rejected';
  end if;

  if exists (
    select 1 from inspection_years
    where id = v_unbound_year_id and standard_profile_id is not null
  ) then
    raise exception '010 smoke: migration unexpectedly backfilled old inspection years';
  end if;

  raise notice '010 smoke passed';
end
$$;

rollback;
