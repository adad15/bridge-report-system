begin;

do $$
declare
  v_bridge_id uuid;
  v_inspection_id uuid;
  v_revision_id uuid;
  v_source_file_id uuid;
  v_photo_file_id uuid;
  v_import_record_id uuid;
  v_component_id uuid;
  v_thread_id uuid;
  v_observation_id uuid;
  v_measurement_count integer;
  v_photo_count integer;
  v_current_count integer;
  v_defect_rows integer;
begin
  insert into bridges (
    bridge_name,
    business_code,
    route_number,
    route_name,
    administrative_region,
    bridge_type,
    span_combination,
    bridge_length_m,
    bridge_width_m,
    maintenance_org
  )
  values (
    '绕阳河二号桥',
    'Q202605001-JZ-024',
    'S319',
    '辽小线',
    '辽宁省',
    '预应力混凝土空心板桥',
    '3x20m',
    66.00,
    12.00,
    '测试管养单位'
  )
  returning id into v_bridge_id;

  insert into bridge_aliases (
    bridge_id,
    alias_name,
    source_type,
    is_manually_confirmed
  )
  values (
    v_bridge_id,
    'S319辽小线绕阳河二号桥',
    '人工录入',
    true
  );

  insert into inspection_years (
    bridge_id,
    inspection_year,
    inspection_date,
    project_name,
    inspection_org,
    report_number,
    status,
    overall_score,
    overall_grade
  )
  values (
    v_bridge_id,
    2026,
    date '2026-06-20',
    '绕阳河二号桥 2026 定期检测',
    '测试检测单位',
    'Q202605001-JZ-024',
    '已确认',
    88.50,
    '2类'
  )
  returning id into v_inspection_id;

  begin
    insert into inspection_years (
      bridge_id,
      inspection_year,
      version_number,
      is_current,
      status
    )
    values (
      v_bridge_id,
      2026,
      99,
      true,
      '待校对'
    );

    raise exception 'Expected unique current inspection constraint to reject duplicate current year';
  exception
    when unique_violation then
      null;
  end;

  insert into archived_files (
    bridge_id,
    inspection_year_id,
    original_file_name,
    current_file_name,
    storage_relative_path,
    file_type,
    file_purpose,
    file_extension,
    file_size_bytes,
    file_hash,
    source_description
  )
  values (
    v_bridge_id,
    v_inspection_id,
    '绕阳河二号桥报告.docx',
    'GDWJ-000001_绕阳河二号桥报告.docx',
    'bridges/QL-000001_绕阳河二号桥/2026/imports/DRJL-000001_软件Word导入/input/GDWJ-000001_绕阳河二号桥报告.docx',
    'Word文档',
    '原始数据源',
    'docx',
    1024,
    'sha256:test-source',
    '用户上传'
  )
  returning id into v_source_file_id;

  insert into import_records (
    bridge_id,
    inspection_year_id,
    import_name,
    source_type,
    import_status,
    main_file_id,
    importer_name,
    importer_version,
    parsed_result_json,
    validation_result_json,
    warning_summary,
    started_at,
    finished_at
  )
  values (
    v_bridge_id,
    v_inspection_id,
    '绕阳河二号桥 2026 软件Word导入',
    '软件导出Word',
    '待校对',
    v_source_file_id,
    '软件Word报告导入器',
    '2026.07.03',
    '{"contract_version":"0.1","bridge_check":{"selected_bridge":"绕阳河二号桥"},"defects":[{"photo_number":"照片2.1-1"}]}'::jsonb,
    '{"photo_number_check":"高置信候选"}'::jsonb,
    '测试导入存在 1 条候选病害',
    now(),
    now()
  )
  returning id into v_import_record_id;

  insert into import_record_files (
    import_record_id,
    archived_file_id,
    file_role,
    process_status,
    process_note
  )
  values (
    v_import_record_id,
    v_source_file_id,
    '主报告',
    '处理成功',
    '主报告已归档'
  );

  insert into bridge_components (
    bridge_id,
    structure_part,
    component_type,
    business_component_code,
    span_number,
    transverse_number,
    side,
    location_description,
    normalized_component_key,
    current_status,
    creation_source
  )
  values (
    v_bridge_id,
    '上部结构',
    '空心板',
    '2-1#板',
    '第2孔',
    '1',
    '全幅',
    '第2跨第1片板',
    'upper|hollow-slab|2-1',
    '已确认',
    '导入沉淀'
  )
  returning id into v_component_id;

  insert into component_aliases (
    bridge_component_id,
    alias_text,
    source_file_id,
    recognition_confidence,
    is_manually_confirmed
  )
  values (
    v_component_id,
    '2-1号板',
    v_source_file_id,
    0.9300,
    true
  );

  insert into defect_threads (
    bridge_id,
    bridge_component_id,
    thread_name,
    defect_type,
    defect_location,
    first_seen_inspection_id,
    latest_seen_inspection_id,
    current_status,
    confirmation_status
  )
  values (
    v_bridge_id,
    v_component_id,
    '2-1#板底板横向裂缝',
    '横向裂缝',
    '底板',
    v_inspection_id,
    v_inspection_id,
    '持续存在',
    '人工已确认'
  )
  returning id into v_thread_id;

  insert into defect_observations (
    inspection_year_id,
    bridge_id,
    bridge_component_id,
    defect_thread_id,
    source_import_record_id,
    source_file_id,
    source_table_title,
    source_table_index,
    source_row_number,
    source_raw_cells_json,
    structure_part,
    part_name,
    component_type,
    business_component_code,
    defect_location,
    defect_type,
    defect_description_raw,
    scale,
    is_repaired,
    extraction_confidence,
    review_status
  )
  values (
    v_inspection_id,
    v_bridge_id,
    v_component_id,
    v_thread_id,
    v_import_record_id,
    v_source_file_id,
    '上部结构病害检查表',
    2,
    1,
    '{"照片编号":"照片2.1-1","病害类型":"横向裂缝"}'::jsonb,
    '上部结构',
    '主梁',
    '空心板',
    '2-1#板',
    '底板',
    '横向裂缝',
    '底板横向裂缝 L=1.2m，W=0.15mm',
    '2',
    '否',
    0.9500,
    '已确认'
  )
  returning id into v_observation_id;

  insert into defect_measurements (
    defect_observation_id,
    measurement_type,
    numeric_value,
    unit,
    raw_text,
    normalized_text,
    is_auto_parsed,
    is_manually_confirmed
  )
  values
    (v_observation_id, '长度', 1.2000, 'm', 'L=1.2m', '长度=1.2m', true, true),
    (v_observation_id, '最大宽度', 0.1500, 'mm', 'W=0.15mm', '最大宽度=0.15mm', true, true);

  insert into archived_files (
    bridge_id,
    inspection_year_id,
    original_file_name,
    current_file_name,
    storage_relative_path,
    file_type,
    file_purpose,
    file_extension,
    file_size_bytes,
    file_hash,
    source_description
  )
  values (
    v_bridge_id,
    v_inspection_id,
    'image1.jpg',
    'GDWJ-000002_照片2.1-1.jpg',
    'bridges/QL-000001_绕阳河二号桥/2026/imports/DRJL-000001_软件Word导入/photos/GDWJ-000002_照片2.1-1.jpg',
    '图片',
    '病害照片',
    'jpg',
    2048,
    'sha256:test-photo',
    'Word内抽取'
  )
  returning id into v_photo_file_id;

  insert into defect_photos (
    defect_observation_id,
    archived_file_id,
    source_import_record_id,
    source_file_id,
    photo_number,
    photo_title,
    photo_description
  )
  values (
    v_observation_id,
    v_photo_file_id,
    v_import_record_id,
    v_source_file_id,
    '照片2.1-1',
    '2-1#板底板横向裂缝',
    '病害检查表照片编号与照片区标题一致'
  );

  update inspection_years
  set is_current = false,
      status = '已被修订',
      updated_at = now()
  where id = v_inspection_id;

  insert into inspection_years (
    bridge_id,
    inspection_year,
    version_number,
    is_current,
    revision_source_inspection_id,
    status,
    report_number,
    overall_score,
    overall_grade
  )
  values (
    v_bridge_id,
    2026,
    2,
    true,
    v_inspection_id,
    '已确认',
    'Q202605001-JZ-024-REV2',
    89.00,
    '2类'
  )
  returning id into v_revision_id;

  insert into defect_comparisons (
    bridge_id,
    defect_thread_id,
    current_inspection_year_id,
    compared_inspection_year_id,
    previous_defect_observation_id,
    current_defect_observation_id,
    comparison_result,
    change_summary,
    suggested_confidence,
    matching_evidence,
    confirmation_status,
    confirmed_at
  )
  values (
    v_bridge_id,
    v_thread_id,
    v_revision_id,
    v_inspection_id,
    v_observation_id,
    null,
    '基本无变化',
    '修订版测试中保留原病害事实，实际对比由后续模块生成。',
    0.8000,
    '{"component":"2-1#板","defect_type":"横向裂缝"}'::jsonb,
    '人工已确认',
    now()
  );

  select count(*) into v_measurement_count
  from defect_measurements
  where defect_observation_id = v_observation_id;

  select count(*) into v_photo_count
  from defect_photos
  where defect_observation_id = v_observation_id;

  select count(*) into v_current_count
  from inspection_years
  where bridge_id = v_bridge_id
    and inspection_year = 2026
    and is_current = true;

  select count(*) into v_defect_rows
  from defect_observations o
  join defect_photos p on p.defect_observation_id = o.id
  where o.bridge_id = v_bridge_id
    and o.inspection_year_id = v_inspection_id
    and o.review_status = '已确认';

  if v_measurement_count <> 2 then
    raise exception 'Expected 2 measurements, got %', v_measurement_count;
  end if;

  if v_photo_count <> 1 then
    raise exception 'Expected 1 photo, got %', v_photo_count;
  end if;

  if v_current_count <> 1 then
    raise exception 'Expected exactly 1 current inspection after revision, got %', v_current_count;
  end if;

  if v_defect_rows <> 1 then
    raise exception 'Expected query to find confirmed defect/photo row, got %', v_defect_rows;
  end if;
end $$;

rollback;
