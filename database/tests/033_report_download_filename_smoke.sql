-- 033 冒烟测试：生成任务的交付文件名与两条配对约束。
-- 事务末尾回滚，不留下测试数据。

begin;

do $$
declare
  v_user_id uuid;
  v_bridge_id uuid;
  v_year_id uuid;
  v_job_id uuid;
  v_violation_caught boolean;
begin
  select id into v_user_id from users order by created_at limit 1;
  if v_user_id is null then
    insert into users (username, display_name, password_hash, role)
    values ('smoke033', 'Smoke 033', 'not-a-real-hash', 'admin')
    returning id into v_user_id;
  end if;

  insert into bridges (bridge_name) values ('033 冒烟桥') returning id into v_bridge_id;
  insert into inspection_years (bridge_id, inspection_year, status)
  values (v_bridge_id, 2026, '已确认')
  returning id into v_year_id;

  insert into report_generation_jobs
    (inspection_year_id, requested_by_user_id, status)
  values (v_year_id, v_user_id, 'assembling_docx')
  returning id into v_job_id;

  -- ready 必须同时有文件和文件名：下载接口两样都要用，少一样就给不出正确的下载。
  v_violation_caught := false;
  begin
    update report_generation_jobs
    set status = 'ready', temporary_file_path = 'reports/job/out.docx'
    where id = v_job_id;
  exception when check_violation then
    v_violation_caught := true;
  end;
  if not v_violation_caught then
    raise exception '033 smoke: ready 任务没有下载文件名却被接受';
  end if;

  v_violation_caught := false;
  begin
    update report_generation_jobs
    set status = 'ready', download_filename = '报告.docx'
    where id = v_job_id;
  exception when check_violation then
    v_violation_caught := true;
  end;
  if not v_violation_caught then
    raise exception '033 smoke: ready 任务没有文件路径却被接受';
  end if;

  update report_generation_jobs
  set status = 'ready',
      temporary_file_path = 'reports/job/out.docx',
      download_filename = '百股大桥定期检测报告（2类）.docx',
      finished_at = now(), expires_at = now() + interval '24 hours'
  where id = v_job_id;

  -- 到期后文件已经删了，路径和文件名必须一起清空：留着一个文件名只会让界面显示出
  -- 一份实际不存在的报告。
  v_violation_caught := false;
  begin
    update report_generation_jobs
    set status = 'expired', temporary_file_path = null
    where id = v_job_id;
  exception when check_violation then
    v_violation_caught := true;
  end;
  if not v_violation_caught then
    raise exception '033 smoke: expired 任务仍保留着下载文件名';
  end if;

  update report_generation_jobs
  set status = 'expired', temporary_file_path = null, download_filename = null
  where id = v_job_id;

  delete from report_generation_jobs where inspection_year_id = v_year_id;
  delete from inspection_years where id = v_year_id;
  delete from bridges where id = v_bridge_id;

  raise notice '033 smoke passed';
end $$;

rollback;
