-- 006 冒烟测试：年度删除审计、文件清理队列及危险字段约束。
-- 事务末尾回滚，不留下测试数据。

begin;

do $$
declare
  v_bridge_id uuid;
  v_user_id uuid;
  v_audit_id uuid;
  v_violation_caught boolean;
begin
  insert into bridges (bridge_name) values ('006删除审计冒烟测试桥') returning id into v_bridge_id;
  insert into users (username, display_name, password_hash, role)
  values ('smoke_delete_admin', '删除测试管理员', 'not-a-real-hash', 'admin')
  returning id into v_user_id;

  insert into inspection_year_deletion_audits (
    bridge_id, bridge_system_number_snapshot, bridge_name_snapshot, inspection_year,
    actor_user_id, actor_username_snapshot, actor_display_name_snapshot, reason,
    impact_json, deleted_counts_json
  )
  select v_bridge_id, system_number, bridge_name, 2026,
         v_user_id, 'smoke_delete_admin', '删除测试管理员', '误建年度',
         '{"counts":{"inspection_versions":2}}'::jsonb,
         '{"inspection_versions":2}'::jsonb
  from bridges where id = v_bridge_id
  returning id into v_audit_id;

  insert into archived_file_deletion_queue (deletion_audit_id, storage_relative_path)
  values (v_audit_id, 'bridges/QL-000001/2026/report.docx');

  update archived_file_deletion_queue
  set status = '已完成', attempt_count = 1, completed_at = now()
  where deletion_audit_id = v_audit_id;

  v_violation_caught := false;
  begin
    insert into archived_file_deletion_queue (deletion_audit_id, storage_relative_path)
    values (v_audit_id, '../outside.docx');
  exception when check_violation then
    v_violation_caught := true;
  end;
  if not v_violation_caught then
    raise exception '006 smoke: unsafe relative path was not rejected';
  end if;

  v_violation_caught := false;
  begin
    update inspection_year_deletion_audits set reason = '   ' where id = v_audit_id;
  exception when check_violation then
    v_violation_caught := true;
  end;
  if not v_violation_caught then
    raise exception '006 smoke: blank deletion reason was not rejected';
  end if;

  v_violation_caught := false;
  begin
    update archived_file_deletion_queue
    set status = '已完成', completed_at = null
    where deletion_audit_id = v_audit_id;
  exception when check_violation then
    v_violation_caught := true;
  end;
  if not v_violation_caught then
    raise exception '006 smoke: completed queue row without timestamp was not rejected';
  end if;

  raise notice '006 smoke passed';
end
$$;

rollback;

