-- B batch: standalone cleanup journal and monotonic location-map revision.
begin;
do $$
declare
  j uuid := gen_random_uuid();
  b uuid;
  f uuid;
  revision bigint;
begin
  -- No job row is required: this record must survive deletion of its business owner.
  insert into report_job_artifacts(job_id,expected_output_path,cleanup_state)
  values(j,'controlled/job/report.docx','pending');
  if not exists(select 1 from report_job_artifacts where job_id=j) then
    raise exception '037: standalone journal was not retained';
  end if;
  begin
    update report_job_artifacts set cleanup_state='unknown' where job_id=j;
    raise exception '037: invalid cleanup state accepted';
  exception when check_violation then null;
  end;
  begin
    update report_job_artifacts set attempts=-1 where job_id=j;
    raise exception '037: negative attempts accepted';
  exception when check_violation then null;
  end;

  insert into bridges(bridge_name) values('037 smoke bridge') returning id into b;
  insert into archived_files(bridge_id,original_file_name,current_file_name,file_extension,file_size_bytes,file_type,file_purpose,storage_relative_path)
  values(b,'map.png','map.png','.png',1,'图片','桥梁图件·地理位置图','smoke037/map.png') returning id into f;
  insert into bridge_media(bridge_id,slot,archived_file_id,source)
  values(b,'LOCATION_MAP',f,'人工上传');
  select location_map_revision into revision from bridges where id=b;
  if revision<>1 then raise exception '037: insert did not increment revision'; end if;
  delete from bridge_media where bridge_id=b;
  if (select location_map_revision from bridges where id=b)<>2 then
    raise exception '037: deletion lost revision (ABA protection)';
  end if;
end $$;
rollback;
