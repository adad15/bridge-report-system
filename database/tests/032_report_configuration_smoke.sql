-- 032 冒烟测试：报告模板、人员库、设备库、年度配置与生成任务的约束和删除语义。
-- 事务末尾回滚，不留下测试数据。

begin;

do $$
declare
  v_user_id uuid;
  v_bridge_id uuid;
  v_year_id uuid;
  v_file_id uuid;
  v_other_file_id uuid;
  v_template_id uuid;
  v_second_template_id uuid;
  v_person_id uuid;
  v_equipment_id uuid;
  v_violation_caught boolean;
  v_count integer;
begin
  select id into v_user_id from users order by created_at limit 1;
  if v_user_id is null then
    insert into users (username, display_name, password_hash, role)
    values ('smoke032', 'Smoke 032', 'not-a-real-hash', 'admin')
    returning id into v_user_id;
  end if;

  insert into bridges (bridge_name) values ('032 冒烟桥') returning id into v_bridge_id;
  insert into inspection_years (bridge_id, inspection_year, status)
  values (v_bridge_id, 2026, '已确认')
  returning id into v_year_id;

  insert into archived_files
    (bridge_id, original_file_name, current_file_name, storage_relative_path,
     file_type, file_purpose)
  values
    (v_bridge_id, 't.docx', 't.docx', 'templates/t.docx', '模板', '报告模板')
  returning id into v_file_id;
  insert into archived_files
    (bridge_id, original_file_name, current_file_name, storage_relative_path,
     file_type, file_purpose)
  values
    (v_bridge_id, 't2.docx', 't2.docx', 'templates/t2.docx', '模板', '报告模板')
  returning id into v_other_file_id;

  -- ---------------------------------------------------------------- 模板约束

  v_violation_caught := false;
  begin
    insert into report_templates
      (template_code, template_name, file_id, file_checksum, is_enabled,
       validation_status, updated_by_user_id)
    values ('bad', '未校验就启用', v_file_id, 'sha256:' || repeat('a', 64),
            true, 'invalid', v_user_id);
  exception when check_violation then
    v_violation_caught := true;
  end;
  if not v_violation_caught then
    raise exception '032 smoke: 校验未通过的模板被允许启用';
  end if;

  insert into report_templates
    (template_code, template_name, file_id, file_checksum, is_enabled, is_default,
     validation_status, updated_by_user_id)
  values ('periodic_v1', '定期检测标准模板', v_file_id, 'sha256:' || repeat('b', 64),
          true, true, 'valid', v_user_id)
  returning id into v_template_id;

  v_violation_caught := false;
  begin
    insert into report_templates
      (template_code, template_name, file_id, file_checksum, is_enabled, is_default,
       validation_status, updated_by_user_id)
    values ('periodic_v2', '第二个默认模板', v_other_file_id, 'sha256:' || repeat('c', 64),
            true, true, 'valid', v_user_id);
  exception when unique_violation then
    v_violation_caught := true;
  end;
  if not v_violation_caught then
    raise exception '032 smoke: 出现了第二个默认模板';
  end if;

  insert into report_templates
    (template_code, template_name, file_id, file_checksum, is_enabled, is_default,
     validation_status, updated_by_user_id)
  values ('periodic_v2', '第二个模板', v_other_file_id, 'sha256:' || repeat('c', 64),
          true, false, 'valid', v_user_id)
  returning id into v_second_template_id;

  -- 模板还在引用时，归档文件不许删除（§17.4）。
  v_violation_caught := false;
  begin
    delete from archived_files where id = v_file_id;
  exception when restrict_violation or foreign_key_violation then
    v_violation_caught := true;
  end;
  if not v_violation_caught then
    raise exception '032 smoke: 被模板引用的归档文件被删掉了';
  end if;

  -- ------------------------------------------------------------ 人员与设备

  insert into report_personnel (full_name, organization, professional_title)
  values ('张三', '某某交通科学研究院', '高级工程师')
  returning id into v_person_id;

  insert into report_equipment (equipment_name, model_spec, calibration_valid_until)
  values ('裂缝观测仪', 'ZBL-F103', date '2027-06-30')
  returning id into v_equipment_id;

  -- ------------------------------------------------------------ 年度配置

  insert into inspection_report_settings
    (inspection_year_id, template_id, configured_by_user_id, configured_at)
  values (v_year_id, v_template_id, v_user_id, now());

  v_violation_caught := false;
  begin
    insert into inspection_report_settings (inspection_year_id, template_id)
    values (v_year_id, v_second_template_id);
  exception when unique_violation then
    v_violation_caught := true;
  end;
  if not v_violation_caught then
    raise exception '032 smoke: 同一年度出现了两份报告配置';
  end if;

  insert into inspection_report_personnel
    (inspection_year_id, personnel_id, role_code, sort_order)
  values (v_year_id, v_person_id, 'approver', 0);

  -- 同一个人可以兼任别的角色。
  insert into inspection_report_personnel
    (inspection_year_id, personnel_id, role_code, sort_order)
  values (v_year_id, v_person_id, 'compiler', 0);

  v_violation_caught := false;
  begin
    insert into inspection_report_personnel
      (inspection_year_id, personnel_id, role_code)
    values (v_year_id, v_person_id, 'approver');
  exception when unique_violation then
    v_violation_caught := true;
  end;
  if not v_violation_caught then
    raise exception '032 smoke: 同一角色重复分配同一个人未被拒绝';
  end if;

  v_violation_caught := false;
  begin
    insert into inspection_report_personnel
      (inspection_year_id, personnel_id, role_code)
    values (v_year_id, v_person_id, '总工');
  exception when check_violation then
    v_violation_caught := true;
  end;
  if not v_violation_caught then
    raise exception '032 smoke: 未知人员角色代码未被拒绝';
  end if;

  insert into inspection_report_equipment
    (inspection_year_id, equipment_id, purpose, sort_order)
  values (v_year_id, v_equipment_id, '裂缝宽度测量', 0);

  -- 被年度配置引用的人员和设备不得硬删除，只能停用（§15.3）。
  v_violation_caught := false;
  begin
    delete from report_personnel where id = v_person_id;
  exception when restrict_violation or foreign_key_violation then
    v_violation_caught := true;
  end;
  if not v_violation_caught then
    raise exception '032 smoke: 被引用的人员被硬删除了';
  end if;

  v_violation_caught := false;
  begin
    delete from report_equipment where id = v_equipment_id;
  exception when restrict_violation or foreign_key_violation then
    v_violation_caught := true;
  end;
  if not v_violation_caught then
    raise exception '032 smoke: 被引用的设备被硬删除了';
  end if;

  -- ------------------------------------------------------------ 生成任务

  insert into report_generation_jobs
    (inspection_year_id, requested_by_user_id, status, template_id)
  values (v_year_id, v_user_id, 'assembling_docx', v_template_id);

  v_violation_caught := false;
  begin
    insert into report_generation_jobs
      (inspection_year_id, requested_by_user_id, status)
    values (v_year_id, v_user_id, 'queued');
  exception when unique_violation then
    v_violation_caught := true;
  end;
  if not v_violation_caught then
    raise exception '032 smoke: 同一用户同一年度出现了两个进行中的任务';
  end if;

  v_violation_caught := false;
  begin
    update report_generation_jobs set status = 'ready'
    where inspection_year_id = v_year_id;
  exception when check_violation then
    v_violation_caught := true;
  end;
  if not v_violation_caught then
    raise exception '032 smoke: ready 任务没有文件路径却被接受';
  end if;

  -- 033 起 ready 还要有 download_filename，两样都要用才给得出正确的下载。
  update report_generation_jobs
  set status = 'ready', temporary_file_path = 'reports/job-1/out.docx',
      download_filename = '百股大桥定期检测报告.docx',
      finished_at = now(), expires_at = now() + interval '24 hours'
  where inspection_year_id = v_year_id;

  v_violation_caught := false;
  begin
    update report_generation_jobs set status = 'expired'
    where inspection_year_id = v_year_id;
  exception when check_violation then
    v_violation_caught := true;
  end;
  if not v_violation_caught then
    raise exception '032 smoke: expired 任务仍保留着临时文件路径';
  end if;

  update report_generation_jobs
  set status = 'expired', temporary_file_path = null, download_filename = null
  where inspection_year_id = v_year_id;

  v_violation_caught := false;
  begin
    update report_generation_jobs set status = 'failed', error_code = null
    where inspection_year_id = v_year_id;
  exception when check_violation then
    v_violation_caught := true;
  end;
  if not v_violation_caught then
    raise exception '032 smoke: 失败任务没有错误码却被接受';
  end if;

  -- 终态之后可以再起一个新任务，重新生成不被旧任务挡住。
  insert into report_generation_jobs
    (inspection_year_id, requested_by_user_id, status)
  values (v_year_id, v_user_id, 'queued');

  -- ------------------------------------------------- 年度删除的级联行为

  delete from report_generation_jobs where inspection_year_id = v_year_id;
  delete from inspection_years where id = v_year_id;

  select count(*) into v_count
  from inspection_report_settings where inspection_year_id = v_year_id;
  if v_count <> 0 then
    raise exception '032 smoke: 年度删除后报告配置仍在';
  end if;

  select count(*) into v_count
  from inspection_report_personnel where inspection_year_id = v_year_id;
  if v_count <> 0 then
    raise exception '032 smoke: 年度删除后人员分配仍在';
  end if;

  select count(*) into v_count
  from inspection_report_equipment where inspection_year_id = v_year_id;
  if v_count <> 0 then
    raise exception '032 smoke: 年度删除后设备分配仍在';
  end if;
end
$$;

rollback;
