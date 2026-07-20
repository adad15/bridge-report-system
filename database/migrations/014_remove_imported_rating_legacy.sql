-- 014：删除 Word 导入评分与旧构件评分校验存储，只保留系统正式评定投影。
--
-- 这是有意的破坏性清理。若库中仍有未绑定 assessment_run 的评分，或病害中仍保存
-- Word 扣分/构件分，迁移会明确失败，要求先删除相应旧业务数据，绝不静默丢弃。

do $$
declare
  v_legacy_ratings bigint;
  v_legacy_defects bigint;
begin
  select count(*) into v_legacy_ratings
  from condition_ratings
  where assessment_run_id is null;

  if v_legacy_ratings > 0 then
    raise exception using
      errcode = 'P0001',
      message = format(
        '014 migration blocked: %s condition_ratings rows are not system assessment projections; delete the legacy bridge/year data first',
        v_legacy_ratings);
  end if;

  if exists (
    select 1 from information_schema.columns
    where table_schema = current_schema()
      and table_name = 'defect_observations'
      and column_name = 'defect_deduction'
  ) then
    execute 'select count(*) from defect_observations where defect_deduction is not null or component_score is not null'
      into v_legacy_defects;
    if v_legacy_defects > 0 then
      raise exception using
        errcode = 'P0001',
        message = format(
          '014 migration blocked: %s defect_observations rows still contain imported deduction/component score data; delete the legacy bridge/year data first',
          v_legacy_defects);
    end if;
  end if;
end
$$;

alter table condition_ratings
  drop constraint if exists condition_ratings_score_validation_status_check;

alter table condition_ratings
  drop column if exists source_score,
  drop column if exists calculated_score,
  drop column if exists score_validation_status,
  drop column if exists score_resolution_reason,
  drop column if exists calculation_details_json;

alter table condition_ratings
  alter column assessment_run_id set not null;

alter table defect_observations
  drop column if exists defect_deduction,
  drop column if exists component_score;

comment on column condition_ratings.assessment_run_id is
  '正式系统评定投影的唯一来源运行；不允许 Word 导入评分或无运行来源的评分。';
