-- 038: 规范原文库的约束。全部在事务里做，最后回滚，不留数据。
begin;
do $$
declare
  d uuid;
  f1 uuid;
  f2 uuid;
  f3 uuid;
begin
  insert into standard_documents(name, category) values('038 冒烟规范', '冒烟') returning id into d;
  insert into archived_files(original_file_name, current_file_name, storage_relative_path, file_type, file_purpose, file_extension)
  values('a.pdf', 'a.pdf', 'standards/smoke038/a.pdf', '附件', '规范原文', '.pdf') returning id into f1;
  insert into archived_files(original_file_name, current_file_name, storage_relative_path, file_type, file_purpose, file_extension)
  values('b.pdf', 'b.pdf', 'standards/smoke038/b.pdf', '附件', '规范原文', '.pdf') returning id into f2;
  insert into archived_files(original_file_name, current_file_name, storage_relative_path, file_type, file_purpose, file_extension)
  values('c.pdf', 'c.pdf', 'standards/smoke038/c.pdf', '附件', '规范原文', '.pdf') returning id into f3;

  insert into standard_document_versions(document_id, label, code, kind, effective_date, archived_file_id)
  values(d, '2011 版', 'JTG/T H21—2011', 'official', '2011-09-01', f1);

  begin
    insert into standard_document_versions(document_id, label, code, kind, archived_file_id)
    values(d, '无实施日期', 'X', 'official', f2);
    raise exception '038: official version without effective date accepted';
  exception when check_violation then null;
  end;

  begin
    insert into standard_document_versions(document_id, label, code, kind, effective_date, archived_file_id)
    values(d, '带实施日期的稿件', 'X', 'draft_comment', '2017-01-01', f2);
    raise exception '038: draft with effective date accepted';
  exception when check_violation then null;
  end;

  begin
    insert into standard_document_versions(document_id, label, code, kind, effective_date, withdrawn_date, archived_file_id)
    values(d, '倒挂', 'X', 'official', '2020-01-01', '2019-01-01', f2);
    raise exception '038: withdrawn before effective accepted';
  exception when check_violation then null;
  end;

  begin
    insert into standard_document_versions(document_id, label, code, kind, effective_date, archived_file_id)
    values(d, '同日另一版', 'X', 'official', '2011-09-01', f2);
    raise exception '038: duplicate official effective date accepted';
  exception when unique_violation then null;
  end;

  begin
    insert into standard_document_versions(document_id, label, code, kind, archived_file_id)
    values(d, '2011 版', 'X', 'draft_comment', f2);
    raise exception '038: duplicate label accepted';
  exception when unique_violation then null;
  end;

  begin
    insert into standard_document_versions(document_id, label, code, kind, archived_file_id)
    values(d, '共用文件', 'X', 'draft_comment', f1);
    raise exception '038: two versions shared one archived file';
  exception when unique_violation then null;
  end;

  -- 稿件不受实施日期唯一约束影响，两份稿件可以并存。
  insert into standard_document_versions(document_id, label, code, kind, publish_date, archived_file_id)
  values(d, '2017 征求意见稿', 'X', 'draft_comment', '2017-06-01', f2);
  insert into standard_document_versions(document_id, label, code, kind, archived_file_id)
  values(d, '2026 报批稿', 'Y', 'draft_approval', f3);

  begin
    delete from archived_files where id = f1;
    raise exception '038: archived file deleted while a version still uses it';
  -- on delete restrict 抛的是 restrict_violation（23001），不是 foreign_key_violation。
  exception when restrict_violation then null;
  end;

  delete from standard_documents where id = d;
  if exists(select 1 from standard_document_versions where document_id = d) then
    raise exception '038: versions survived their document';
  end if;

  begin
    insert into standard_documents(name, category) values('  ', '冒烟');
    raise exception '038: blank name accepted';
  exception when check_violation then null;
  end;
end $$;
rollback;
