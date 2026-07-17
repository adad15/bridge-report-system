-- 009 冒烟测试：导入删除审计、文件队列、路径约束和审计快照保留。
begin;

do $$
declare
  v_bridge_id uuid;
  v_year_id uuid;
  v_import_id uuid;
  v_user_id uuid;
  v_audit_id uuid;
  v_violation_caught boolean;
begin
  insert into bridges (bridge_name) values ('009导入删除冒烟测试桥') returning id into v_bridge_id;
  insert into inspection_years (bridge_id, inspection_year, version_number)
  values (v_bridge_id, 2026, 1) returning id into v_year_id;
  insert into import_records (bridge_id, inspection_year_id, import_name, source_type, import_status)
  values (v_bridge_id, v_year_id, '009测试导入', '软件导出Word', '待校对') returning id into v_import_id;
  insert into users (username, display_name, password_hash, role)
  values ('smoke_import_delete_admin', '导入删除测试管理员', 'not-a-real-hash', 'admin') returning id into v_user_id;

  insert into import_record_deletion_audits (
    original_import_record_id, import_system_number_snapshot, import_name_snapshot,
    import_status_snapshot, source_type_snapshot, bridge_id, bridge_system_number_snapshot,
    bridge_name_snapshot, inspection_year_id, inspection_year_snapshot, inspection_version_snapshot,
    actor_user_id, actor_username_snapshot, actor_display_name_snapshot, reason
  )
  select ir.id, ir.system_number, ir.import_name, ir.import_status, ir.source_type,
         b.id, b.system_number, b.bridge_name, iy.id, iy.inspection_year, iy.version_number,
         v_user_id, 'smoke_import_delete_admin', '导入删除测试管理员', '误传测试资料'
  from import_records ir
  join bridges b on b.id = ir.bridge_id
  join inspection_years iy on iy.id = ir.inspection_year_id
  where ir.id = v_import_id
  returning id into v_audit_id;

  insert into import_record_file_deletion_queue (
    import_record_deletion_audit_id, storage_kind, artifact_kind, storage_relative_path
  ) values
    (v_audit_id, '临时Word存储', '文件', '123e4567-e89b-12d3-a456-426614174000.docx'),
    (v_audit_id, '归档存储', '解析工作目录', 'work/word-import/123e4567-e89b-12d3-a456-426614174000-acde12');

  update import_record_file_deletion_queue
  set status='清理中', processing_started_at=now()
  where import_record_deletion_audit_id=v_audit_id;
  update import_record_file_deletion_queue
  set status='已完成', processing_started_at=null, completed_at=now()
  where import_record_deletion_audit_id=v_audit_id;

  v_violation_caught := false;
  begin
    insert into import_record_file_deletion_queue (
      import_record_deletion_audit_id, storage_kind, artifact_kind, storage_relative_path
    ) values (v_audit_id, '临时Word存储', '文件', '../outside.docx');
  exception when check_violation then
    v_violation_caught := true;
  end;
  if not v_violation_caught then
    raise exception '009 smoke: unsafe temporary Word path was not rejected';
  end if;

  delete from import_records where id=v_import_id;
  if not exists (
    select 1 from import_record_deletion_audits
    where id=v_audit_id and original_import_record_id=v_import_id
      and import_system_number_snapshot is not null and reason='误传测试资料'
  ) then
    raise exception '009 smoke: deletion audit snapshot was not retained';
  end if;

  raise notice '009 smoke passed';
end
$$;

rollback;
